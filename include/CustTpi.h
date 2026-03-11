#pragma once
#ifndef CUST_TPI_H
#define CUST_TPI_H
#include "main.h"
#include "ThostFtdcTraderApi.h"
#include "ThostFtdcUserApiDataType.h"
#include "ThostFtdcUserApiStruct.h"
#include "LoadConfig.h" 
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <mutex>
#include <condition_variable> 
#include <atomic>   
#include "Logger.h"
namespace fs = std::filesystem;
using namespace std;


//订单状态
struct OrderStatus {
    std::string InstrumentID;
    char Direction = '\0';
    int VolumeTotalOriginal = 0;
    int VolumeTraded = 0;
    char orderStatus = THOST_FTDC_OST_Unknown; // 来自 OnRtnOrder
    bool rejected = false;                     // 下单被拒
    std::string errorMsg;                      // 错误信息

    // 撤单必需字段（来自 OnRtnOrder）
    int FrontID = 0;    // 前置编号
    int SessionID = 0; // 会话编号

    bool isFilled() const { return VolumeTraded >= VolumeTotalOriginal; }   // 是否全部成交
    bool isActive() const {        // 是否仍然有效
        return !rejected &&
            orderStatus != THOST_FTDC_OST_Canceled &&
            orderStatus != THOST_FTDC_OST_AllTraded;
    }
};

// 持仓
struct hold {
    int today_buy_volume = 0;       // 今多手数
    int yesterday_buy_volume = 0;   // 昨多手数
    int today_sell_volume = 0;      // 今空手数
    int yesterday_sell_volume = 0;  // 昨空手数
};


// 连接的状态枚举
enum class ConnectionState {
    DISCONNECTED,      // 初始或断开状态
    CONNECTED,         // 已连上前置（但未认证）
    AUTHENTICATED,     // 已认证
    LOGGED_IN,         // 已登录，可交易
    SETTLEMENT_CONFIRMED,  // 结算单已经确认
};



class CustTpi : public CThostFtdcTraderSpi {
private:
    std::string BROKER_ID;
    std::string USER_ID;
    std::string InvestorID;
    std::string USER_PAW;
    std::string AuthCode;
    std::string AppID;
    std::string UserProductInfo;
    std::string TradeFront;
    std::string CurrencyID;

    CThostFtdcTraderApi* m_pUserApi = nullptr;

    // === 状态同步机制（支持重连）===
    std::mutex m_state_mtx;
    std::condition_variable m_state_cv;
    std::atomic<ConnectionState> m_current_state{ ConnectionState::DISCONNECTED };
    std::atomic<bool> m_has_ever_connected{ false }; // 用于识别是否是重连

    //  === 用于合约查询同步机制===
    mutable std::mutex m_instrument_mutex;
    std::condition_variable m_instrument_cv;
    std::atomic<bool> m_instrument_query_done{ false };
    std::vector<CThostFtdcInstrumentField> Instrument_vector;      // 存储所有合约

   
    // === 用于持仓查询同步机制 ===
    mutable std::mutex m_holdMutex; 
    std::atomic<bool> m_position_query_done{ false };
    std::condition_variable m_pos_wait_cv;
    std::unordered_map<string, hold> today_hold;  // 存储当日所有持仓



    // === 用于委托查询的同步机制 ===
    mutable std::mutex m_todayOrdersMutex;           // 保护 m_today_orders 的互斥锁
    std::atomic<bool> m_order_query_done{ false };   // 查询完成标志
    std::condition_variable m_order_wait_cv;         // 条件变量
    std::vector<CThostFtdcOrderField> today_orders;  // 存储当日所有委托


    // === 用于资金账户查询的同步机制 ===
    mutable std::mutex m_AccountMutex;           
    std::atomic<bool> m_account_query_done{ false };   // 查询完成标志
    std::condition_variable m_account_wait_cv;         // 条件变量
    CThostFtdcTradingAccountField today_account;      // 存储当前的资金账户信息

   
    // 阈值设置
    int m_order_insert_threshold = 10;      // 报单阈值
    int m_order_action_threshold = 10;       // 撤单阈值
    int m_duplicate_order_threshold = 2;     // 重复订单阈值

public:
    CustTpi() {
        // 从单例配置中读取
        const CtpConfig& cfg = ConfigLoader::getInstance();
        BROKER_ID = cfg.BrokerID;
        USER_ID = cfg.UserID;
        InvestorID = cfg.InvestorID;
        USER_PAW = cfg.Password;
        AuthCode = cfg.AuthCode;
        AppID = cfg.AppID;
        UserProductInfo = cfg.ProductInfo;
        TradeFront = cfg.TradeFront; // 读取交易前置
        CurrencyID = cfg.CurrencyID;
        if (!cfg.isValid()) {
            // 处理错误，例如抛出异常或记录日志后退出
            throw std::runtime_error("CTP Configuration load failed!");
        }
    }


    int nRequestID = 0;

    // ===状态控制接口 ===
    void wait_for_state(ConnectionState target);
    void set_state(ConnectionState state);
    void connect();
    void OnFrontConnected() override;
    void OnFrontDisconnected(int nReason) override;
    void Authenticate();
    void OnRspAuthenticate(CThostFtdcRspAuthenticateField* pRspAuthenticateField, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void login();
    void OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;

    // 请求查询上一个交易日结算单并确认
    void reqQrySettlementInfo();
    // 查询结算单回调函数
    void OnRspQrySettlementInfo(CThostFtdcSettlementInfoField* pSettlementInfo, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    // 确认结算单回调函数
    void OnRspSettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField* pSettlementInfoConfirm, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;



    // 请求查询全部合约
    void getAllInstrument();
    // 请求查询单个合约 (从本地内存查询),使用 std::optional 包装，安全地表示“有值”或“无值”
    std::optional<CThostFtdcInstrumentField> getSingleInstrumentLocal(const char* instrumentID) const;
    // 请求查询合约回调函数
    void OnRspQryInstrument(CThostFtdcInstrumentField* pInstrument, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    // 阻塞20s等待查询完成
    bool waitForInstrumentQuery();
    // 提供获取合约列表的接口（避免直接访问私有成员）
    std::vector<CThostFtdcInstrumentField> getInstrumentVector() const;





    ///请求查询投资者全部持仓
    void getAllPosition();
    // 请求获取指定合约的持仓快照 (从本地内存查询)，查询之前需要先调用一下getAllPosition
    hold getSinglePosition(const std::string& instrumentID) const;
    // 请求查询持仓回调函数
    void OnRspQryInvestorPosition(CThostFtdcInvestorPositionField* pInvestorPosition, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    // 阻塞10s等待持仓查询完成
    bool waitForPositionQuery();
    // 提供获取投资者持仓的接口（避免直接访问私有成员）
    std::unordered_map<std::string, hold> getPositionMap() const;



    // 查询当日所有委托
    void getAllOrder(); 
    // 请求查询当日委托回调函数
    virtual void OnRspQryOrder(CThostFtdcOrderField* pOrder, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    // 同步委托查询信号
    bool waitForOrderQuery(); 
    // 打印当日所有委托，按委托时间升序，分“完全成交”和“未完全成交”两组
    void printAllOrdersByInsertTime() const; 
    // 提供获取当日委托的接口（避免直接访问私有成员）
    std::vector<CThostFtdcOrderField> getOrderVector() const; 



    // 设置阈值的接口
    void setOrderInsertThreshold(int threshold);
    void setOrderActionThreshold(int threshold);
    // 查询当日所有委托(报单+撤单)笔数
    int getTodayOrderInsertCount();
    // 查询当日撤单笔数
    int getTodayOrderCanceledCount();
    // 实时检查并告警的函数（基于today_orders）
    bool checkAndWarnOnInsert();
    bool checkAndWarnOnAction(); // 撤单只关心总数+1




    ///请求查询资金账户
    void getTradingAccount();
    ///请求查询资金账户回调函数
    void OnRspQryTradingAccount(CThostFtdcTradingAccountField* pTradingAccount, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    // 同步资金账号查询信号
    bool waitForAccountQuery(); 
    // 获取资金账户副本
    CThostFtdcTradingAccountField getTradingAccount() const;



    
    // 包含订单追踪的下单
    void orderInsertWithTracking(const std::string& orderRef,char* InstrumentID, char* ExchangeID, char Direction, char CombOffsetFlag,double LimitPrice, int VolumeTotalOriginal);
    void OnRspOrderInsert(CThostFtdcInputOrderField* pInputOrder,CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void OnErrRtnOrderInsert(CThostFtdcInputOrderField* pInputOrder, CThostFtdcRspInfoField* pRspInfo) override;
    void OnRtnOrder(CThostFtdcOrderField* pOrder) override;
    void OnRtnTrade(CThostFtdcTradeField* pTrade) override;



    ///报单操作请求  撤单，改价
    void orderCancelWithTracking(const std::string& orderRef);
    void OnRspOrderAction(CThostFtdcInputOrderActionField* pInputOrderAction, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast);
    void OnErrRtnOrderAction(CThostFtdcOrderActionField* pOrderAction, CThostFtdcRspInfoField* pRspInfo);
    // 撤销所有的委托
    void cancelAllUntradedOrders();
};
#endif // CUST_TPI_H