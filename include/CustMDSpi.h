#pragma once
#ifndef CUST_MD_SPI_H
#define CUST_MD_SPI_H
#include "main.h"
#include "ThostFtdcMdApi.h"
#include "ThostFtdcUserApiDataType.h"
#include "ThostFtdcUserApiStruct.h"
#include "LoadConfig.h" 
#include <string>
#include <vector>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>  

namespace fs = std::filesystem;
using namespace std;

class CustMDSpi : public CThostFtdcMdSpi {
private:
    std::string BROKER_ID;
    std::string USER_ID;
    std::string InvestorID;
    std::string USER_PAW;
    std::string AuthCode;
    std::string AppID;
    std::string UserProductInfo;
    std::string MDFront;
    std::string CurrencyID;

    CThostFtdcMdApi* m_mdApi = nullptr;
    vector<string> Subscribe_Instrument_Vector;
    mutable std::shared_mutex m_tickMutex; // 读多写少，用 shared_mutex 提升并发性能
    std::map<std::string, CThostFtdcDepthMarketDataField> m_latestTicks;  // 最新只读快照

public:
    CustMDSpi() {
        // 从单例配置中读取
        const CtpConfig& cfg = ConfigLoader::getInstance();
        BROKER_ID = cfg.BrokerID;
        USER_ID = cfg.UserID;
        InvestorID = cfg.InvestorID;
        USER_PAW = cfg.Password;
        AuthCode = cfg.AuthCode;
        AppID = cfg.AppID;
        UserProductInfo = cfg.ProductInfo;
        MDFront = cfg.MdFront; // 读取行情前置
        CurrencyID = cfg.CurrencyID;
        if (!cfg.isValid()) {
            // 处理错误，例如抛出异常或记录日志后退出
            throw std::runtime_error("CTP Configuration load failed!");
        }
    }

    // 获取只读快照（线程安全）
    std::map<std::string, CThostFtdcDepthMarketDataField> getLatestTicks() const {
        std::shared_lock<std::shared_mutex> lock(m_tickMutex);
        return m_latestTicks;
    }

    void set_Subscribe_Instrument_Vector_vector(vector<string> p);
    void connect();
    void subscribe();

    // 回调
    void OnFrontConnected() override;
    void OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin,
        CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* pDepthMarketData) override;
    void OnFrontDisconnected(int nReason) override;
};

#endif // CUST_MD_SPI_H