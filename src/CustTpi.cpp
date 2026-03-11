#include "CustTpi.h"
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <map>
#include <algorithm>
#include <iomanip>
#include <vector>
#include <shared_mutex>   
#include "ThostFtdcUserApiDataType.h"
#include "ConvertEncoding.h"


// 同步连接状态
void CustTpi::set_state(ConnectionState state) {
    {
        std::lock_guard<std::mutex> lock(m_state_mtx);
        m_current_state.store(state);
    }  // 离开作用域！lock 对象被销毁 → 析构函数自动调用 m_state_mtx.unlock()
    m_state_cv.notify_all();   // 唤醒所有正在等待该条件变量的线程
}
void CustTpi::wait_for_state(ConnectionState target) {
    std::unique_lock<std::mutex> lock(m_state_mtx);
    m_state_cv.wait(lock, [this, target]() {
        return m_current_state.load() >= target;   // 请让当前线程等待，直到 m_current_state >= target 这个条件成立
        });
}


// 登录认证部分
void CustTpi::connect() {
    std::cout << "正在连接交易柜台" << std::endl;
    fs::create_directories("./flow");
    if (!m_pUserApi) {
        m_pUserApi = CThostFtdcTraderApi::CreateFtdcTraderApi("./flow/");
        m_pUserApi->RegisterSpi(this);
        m_pUserApi->SubscribePrivateTopic(THOST_TERT_QUICK);
        m_pUserApi->SubscribePublicTopic(THOST_TERT_QUICK);
        m_pUserApi->RegisterFront(const_cast<char*>(TradeFront.c_str()));
        m_pUserApi->Init(); // 异步，不阻塞
    }
}
void CustTpi::OnFrontConnected() {
    if (m_has_ever_connected.load()) {
        Logger::instance().info("【重连成功】已重新连接到交易柜台");
        // 重连后必须重新认证
        Authenticate();
    }
    else {
        Logger::instance().info("【连接成功】首次连接到交易柜台");
        m_has_ever_connected.exchange(true);
    }
    set_state(ConnectionState::CONNECTED);
}
void CustTpi::OnFrontDisconnected(int nReason) {
    /*std::cout << "【连接断开】原因码: " << nReason << std::endl;*/
    Logger::instance().warn("【连接断开】原因码:" + std::to_string(nReason));
    set_state(ConnectionState::DISCONNECTED);
}
void CustTpi::Authenticate() {
    std::ostringstream oss;
    oss << "正在和交易柜台进行认证: " << m_pUserApi->GetApiVersion() << std::endl;
    CThostFtdcReqAuthenticateField field = { 0 };
    strcpy(field.BrokerID, BROKER_ID.c_str());
    strcpy(field.UserID, USER_ID.c_str());
    strcpy(field.UserProductInfo, UserProductInfo.c_str());
    strcpy(field.AuthCode, AuthCode.c_str());
    strcpy(field.AppID, AppID.c_str());
    m_pUserApi->ReqAuthenticate(&field, nRequestID++);
    oss << "API version: " << m_pUserApi->GetApiVersion() << std::endl;
    Logger::instance().info(oss.str());
}
void CustTpi::OnRspAuthenticate(CThostFtdcRspAuthenticateField* pRspAuthenticateField, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
    std::ostringstream oss;
    if (pRspInfo && pRspInfo->ErrorID != 0) {
        oss << "【认证失败】" << convertCTPString(pRspInfo->ErrorMsg) << std::endl;
        Logger::instance().warn(oss.str());
        set_state(ConnectionState::DISCONNECTED);
    }
    else {
        oss << "【认证成功】" << std::endl;
        Logger::instance().info(oss.str());
        set_state(ConnectionState::AUTHENTICATED);
        login(); 
    }
}
void CustTpi::login() {
    Logger::instance().info("正在登陆交易柜台");
    CThostFtdcReqUserLoginField loginField = { 0 };
    strcpy(loginField.BrokerID, BROKER_ID.c_str());
    strcpy(loginField.UserID, USER_ID.c_str());
    strcpy(loginField.Password, USER_PAW.c_str());
    m_pUserApi->ReqUserLogin(&loginField, 1);
}
void CustTpi::OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin,CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
    std::ostringstream oss;
    if (pRspInfo && pRspInfo->ErrorID != 0) {
        oss << "【登录失败】" << convertCTPString(pRspInfo->ErrorMsg) << std::endl;
        Logger::instance().warn(oss.str());
        set_state(ConnectionState::AUTHENTICATED); // 可回退
    }
    else {
        oss << "【登录成功】交易日: " << pRspUserLogin->TradingDay << std::endl;
        Logger::instance().info(oss.str());
        reqQrySettlementInfo();
        set_state(ConnectionState::LOGGED_IN);
    }
}
// 请求查询上一个交易日结算单
void CustTpi::reqQrySettlementInfo() {
    CThostFtdcQrySettlementInfoField qry = { 0 };
    strcpy(qry.BrokerID, BROKER_ID.c_str());
    strcpy(qry.InvestorID, InvestorID.c_str());
    // TradingDay 留空，表示查询上一交易日的结算单
    int ret = m_pUserApi->ReqQrySettlementInfo(&qry, ++nRequestID);
    if (ret != 0) {
        std::ostringstream oss;
        oss << "ReqQrySettlementInfo failed, error code: " << ret;
        Logger::instance().trade(oss.str());
    }
    else {
        Logger::instance().trade("已发送结算单查询请求");
    }
}
// 结算单查询回调
void CustTpi::OnRspQrySettlementInfo(CThostFtdcSettlementInfoField* pSettlementInfo, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
    if (pRspInfo && pRspInfo->ErrorID != 0) {
        std::string errorMsg = pRspInfo ? convertCTPString(pRspInfo->ErrorMsg) : "未知错误";
        std::ostringstream oss;
        oss << "OnRspQrySettlementInfo failed: [" << pRspInfo->ErrorID << "] " << errorMsg;
        Logger::instance().trade(oss.str());
        // 即使失败，也可以尝试确认（部分柜台允许），但这里保守处理：不确认
        return;
    }
    // 可选：记录结算单内容（通常很长，仅调试时开启）
     if (pSettlementInfo) {
         std::string content = convertCTPString(pSettlementInfo->Content);
         Logger::instance().trade("收到结算单片段" + content);
     }
    // 当 bIsLast 为 true 时，表示所有结算信息接收完毕，可以确认
    if (bIsLast) {
        CThostFtdcSettlementInfoConfirmField confirm = { 0 };
        strcpy(confirm.BrokerID, BROKER_ID.c_str());
        strcpy(confirm.InvestorID, InvestorID.c_str());
        int ret = m_pUserApi->ReqSettlementInfoConfirm(&confirm, ++nRequestID);
        if (ret != 0) {
            std::ostringstream oss;
            oss << "ReqSettlementInfoConfirm failed, error code: " << ret;
            Logger::instance().trade(oss.str());
        }
        else {
            Logger::instance().trade("已发送结算单确认请求");
        }
    }
}
// 新增：结算单确认回调
void CustTpi::OnRspSettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField* pSettlementInfoConfirm, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
    if (pRspInfo && pRspInfo->ErrorID != 0) {
        Logger::instance().error("结算单确认失败: " + std::string(pRspInfo->ErrorMsg));
    }
    else {
        Logger::instance().info("【结算单确认成功】");
    }
    set_state(ConnectionState::SETTLEMENT_CONFIRMED); // 新增状态
}




///请求全部查询合约响应
void CustTpi::getAllInstrument() {
    // 1. 加锁：重置状态和清空数据
    {
        std::lock_guard<std::mutex> lock(m_instrument_mutex);
        Instrument_vector.clear();
        m_instrument_query_done.store(false);
    }
    // 2. 发送请求 (不持锁，避免阻塞)
    CThostFtdcQryInstrumentField req = { 0 };
    // 留空表示查询所有
    int ret = m_pUserApi->ReqQryInstrument(&req, nRequestID++);
    if (ret != 0) {
        // 如果发送失败，通知等待者，防止死锁
        std::lock_guard<std::mutex> lock(m_instrument_mutex);
        m_instrument_query_done.store(true);
        m_instrument_cv.notify_all();
        std::cerr << "发送全合约查询请求失败，错误码: " << ret << std::endl;
    }
}
/// 请求查询单个合约
std::optional<CThostFtdcInstrumentField> CustTpi::getSingleInstrumentLocal(const char* instrumentID) const {
    // 1. 参数校验
    if (!instrumentID || strlen(instrumentID) == 0) {
        std::cerr << "错误：合约代码不能为空" << std::endl;
        return std::nullopt; // 返回空值
    }
    std::string target_id = instrumentID;
    // 2. 加锁 (即使是读操作，为了与写入操作同步，且防止迭代过程中 vector 被修改)
    std::lock_guard<std::mutex> lock(m_instrument_mutex);
    // 3. 线性查找
    for (const auto& inst : Instrument_vector) {
        // 如果你确实想查 ExchangeInstID，请改回 inst.ExchangeInstID
        if (strcmp(inst.InstrumentID, target_id.c_str()) == 0) {
            // 4. 返回值的副本 (Copy)
            // 这里会发生一次结构体拷贝，将 inst 的内容复制到一个新对象中返回
            return inst;
        }
    }

    // 5. 未找到
    std::cout << "[本地查询] 未在内存中找到合约: " << target_id << " (可能尚未初始化或已退市)" << std::endl;
    return std::nullopt; // 返回空值，替代原来的 nullptr
}
///请求查询合约响应回调函数
void CustTpi::OnRspQryInstrument(CThostFtdcInstrumentField* pInstrument,CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
    bool has_error = (pRspInfo && pRspInfo->ErrorID != 0);
    size_t current_size = 0;
    // === 加锁处理数据 (速度要快) ===
    {
        std::lock_guard<std::mutex> lock(m_instrument_mutex);
        if (has_error) {
            // 记录错误日志 (尽量简短)
            if (bIsLast) {
                std::cerr << "【合约查询失败】错误码: " << pRspInfo->ErrorID << std::endl;
                m_instrument_query_done.store(true);
                m_instrument_cv.notify_all();
            }
            return;
        }
        if (pInstrument) {
            CThostFtdcInstrumentField newInst;
            // 使用 memcpy 安全复制整个结构体
            memcpy(&newInst, pInstrument, sizeof(CThostFtdcInstrumentField));
            Instrument_vector.emplace_back(newInst);
        }
        // 如果是最后一包
        if (bIsLast) {
            current_size = Instrument_vector.size();
            m_instrument_query_done.store(true);
            // 唤醒等待线程
            m_instrument_cv.notify_all();
        }
    }
    // === 锁已释放 ===
    // === 锁外处理耗时逻辑 (打印、业务回调) ===
    if (bIsLast && !has_error) {
        // 1. 打印日志 (此时不持锁，不会阻塞其他线程)
        std::cout << "合约查询完成，共收到 " << current_size << " 个期货合约" << std::endl;
        // 如果需要打印详细列表，在这里打印是安全的，但依然建议不要全打出来，太卡
        // for (const auto& inst : Instrument_vector) { ... }
    }
}
/// 同步合约查询信号
bool CustTpi::waitForInstrumentQuery() {
    std::unique_lock<std::mutex> lock(m_instrument_mutex);
    // 使用 wait_for 防止因网络问题导致永久阻塞
    // 合约数据量大，传输可能需要几秒到十几秒，timeout 设大一点
    bool success = m_instrument_cv.wait_for(lock, std::chrono::seconds(20),
        [this]() {
            return m_instrument_query_done.load();
        });
    if (!success) {
        std::cerr << "【警告】合约查询超时！未在20秒内收到完整数据。" << std::endl;
        return false;
    }
    return true;
}
/// 获取全部合约的接口，防止访问私有变量
std::vector<CThostFtdcInstrumentField> CustTpi::getInstrumentVector() const {
    std::lock_guard<std::mutex> lock(m_instrument_mutex);
    return Instrument_vector;
}



///请求查询投资者持仓
void CustTpi::getAllPosition() {
    // 1. 加锁：重置状态和清空数据
    {
        std::lock_guard<std::mutex> lock(m_holdMutex);
        today_hold.clear(); // 清空上次结果
        m_position_query_done.store(false); // 重置状态，防止旧状态干扰
    }
    CThostFtdcQryInvestorPositionField req = { 0 };
    strcpy(req.BrokerID, BROKER_ID.c_str());
    strcpy(req.InvestorID, InvestorID.c_str());
    // 发送请求 (此操作无需持锁，避免阻塞其他线程读取或回调处理)
    int ret = m_pUserApi->ReqQryInvestorPosition(&req, nRequestID++);
    if (ret != 0) {
        // 如果发送失败，手动通知等待者，防止死锁
        std::lock_guard<std::mutex> lock(m_holdMutex);
        m_position_query_done.store(true); // 标记为完成（虽然是失败的）
        m_pos_wait_cv.notify_all();
        Logger::instance().error("发送持仓查询请求失败，错误码：" + std::to_string(ret));
    }
}
/// 根据today_hold查询当前某个合约的持仓，使用前需要先调用queryPosition更新today_hold
hold CustTpi::getSinglePosition(const std::string& instrumentID) const {
    std::lock_guard<std::mutex> lock(m_holdMutex);
    auto it = today_hold.find(instrumentID);
    if (it != today_hold.end()) {
        return it->second;
    }
    // 未找到 → 返回空持仓
    return hold{ 0, 0, 0, 0 };
}
///请求查询投资者持仓回调函数
void CustTpi::OnRspQryInvestorPosition(CThostFtdcInvestorPositionField* pInvestorPosition, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
    bool has_error = (pRspInfo && pRspInfo->ErrorID != 0);
    // 全程持有 m_holdMutex，保证数据更新和状态通知的原子性
    {
        std::lock_guard<std::mutex> lock(m_holdMutex);
        if (has_error) {
            std::ostringstream oss;
            oss << "【持仓查询失败】错误码: " << pRspInfo->ErrorID << ", 错误信息: " << (pRspInfo->ErrorMsg ? pRspInfo->ErrorMsg : "Unknown");
            Logger::instance().error(oss.str());

            // 即使出错，如果是最后一包，也要通知等待者，防止死锁
            if (bIsLast) {
                m_position_query_done.store(true);
                m_pos_wait_cv.notify_all();
            }
            return;
        }
        // 解析并更新数据
        if (pInvestorPosition) {
            std::string instrumentID(pInvestorPosition->InstrumentID);
            char posDirection = pInvestorPosition->PosiDirection;
            // 初始化 hold 结构（如果不存在）
            if (today_hold.find(instrumentID) == today_hold.end()) {
                today_hold[instrumentID] = hold{ 0, 0, 0, 0 };
            }
            auto& h = today_hold[instrumentID];
            // 注意：CTP 中一个合约可能有多条记录（多/空分开），所以不能覆盖，要累加或分别处理
            if (posDirection == THOST_FTDC_PD_Long) {
                // 多头持仓
                h.yesterday_buy_volume += static_cast<int>(pInvestorPosition->YdPosition);
                // 今多=多头持仓总仓位-昨多
                h.today_buy_volume += static_cast<int>(pInvestorPosition->Position - pInvestorPosition->YdPosition);
            }
            else if (posDirection == THOST_FTDC_PD_Short) {
                // 空头持仓
                h.yesterday_sell_volume += static_cast<int>(pInvestorPosition->YdPosition);
                // 今空=总仓位-昨空
                h.today_sell_volume += static_cast<int>(pInvestorPosition->Position - pInvestorPosition->YdPosition);
            }
        }
        // 如果是最后一包，通知等待线程
        if (bIsLast) {
            std::ostringstream oss;
            oss << "【持仓查询完成】共收到 " << today_hold.size() << " 个合约的持仓数据。";
            Logger::instance().info(oss.str());

            // 调试打印 (注意：此时持有锁，打印大量日志会阻塞其他线程，生产环境建议移出锁外或限制频率)
            for (const auto& [inst, h] : today_hold) {
                std::cout << "合约: " << inst
                    << " | 今多: " << h.today_buy_volume
                    << " | 昨多: " << h.yesterday_buy_volume
                    << " | 今空: " << h.today_sell_volume
                    << " | 昨空: " << h.yesterday_sell_volume << std::endl;
            }
            m_position_query_done.store(true);
            m_pos_wait_cv.notify_all(); // 唤醒所有等待者
        }
    }
    // 锁在此处析构作用域时释放
}
/// 同步持仓查询信号
bool CustTpi::waitForPositionQuery() {
    std::unique_lock<std::mutex> lock(m_holdMutex);
    // 使用 wait_for 替代 wait，增加健壮性
    bool success = m_pos_wait_cv.wait_for(lock, std::chrono::seconds(10),
        [this]() {
            return m_position_query_done.load();
        });
    if (!success) {
        Logger::instance().error("【警告】持仓查询超时！未在规定时间10s内收到完整数据。");
        return false;
    }
    // 查询成功，重置状态以便下次使用
    m_position_query_done.store(false);
    // 注意：此时锁已释放，today_hold 中的数据是安全的（直到下一次 queryPosition 清空它）
    return true;
}
/// 提供获取投资者持仓的接口(避免直接访问私有成员)，使用前需要先调用queryPosition更新today_hold
std::unordered_map<std::string, hold> CustTpi::getPositionMap() const {
    std::lock_guard<std::mutex> lock(m_holdMutex);
    return today_hold;
}



/// 查询当日委托
void CustTpi::getAllOrder() {
    // 1. 加锁：重置状态和清空数据
    {
        std::lock_guard<std::mutex> lock(m_todayOrdersMutex);
        today_orders.clear();             // 清空上次结果
        m_order_query_done.store(false);  // 重置状态，防止旧状态干扰
    }
    // 2. 构造请求包
    CThostFtdcQryOrderField qry = { 0 }; // 使用列表初始化清零
    strncpy(qry.BrokerID, BROKER_ID.c_str(), sizeof(qry.BrokerID) - 1);
    strncpy(qry.InvestorID, InvestorID.c_str(), sizeof(qry.InvestorID) - 1);
    // 其他字段保持为空（表示查全部）

    // 3. 发送请求 (此操作无需持锁，避免阻塞回调线程)
    int ret = m_pUserApi->ReqQryOrder(&qry, nRequestID++);
    if (ret != 0) {
        // 如果发送失败，手动通知等待者，防止死锁
        std::lock_guard<std::mutex> lock(m_todayOrdersMutex);
        m_order_query_done.store(true);
        m_order_wait_cv.notify_all();
        Logger::instance().error("【错误】发送委托查询请求失败，错误码：" + std::to_string(ret));
    }
}
/// 分组打印当日委托的订单，使用前需要先调用getAllOrder更新today_orders
void CustTpi::printAllOrdersByInsertTime() const {
    // 1. 加锁拷贝数据，避免在排序和打印过程中数据被修改
    std::vector<CThostFtdcOrderField> orders_copy;
    {
        std::lock_guard<std::mutex> lock(m_todayOrdersMutex);
        orders_copy = today_orders;
    }
    if (orders_copy.empty()) {
        std::cout << "当日无任何委托记录。\n";
        return;
    }
    // 按 InsertDate + InsertTime 升序排序
    std::sort(orders_copy.begin(), orders_copy.end(),
        [](const CThostFtdcOrderField& a, const CThostFtdcOrderField& b) {
            std::string timeA = std::string(a.InsertDate ? a.InsertDate : "") + std::string(a.InsertTime ? a.InsertTime : "");
            std::string timeB = std::string(b.InsertDate ? b.InsertDate : "") + std::string(b.InsertTime ? b.InsertTime : "");
            return timeA < timeB;
        });
    // 分组容器
    std::vector<const CThostFtdcOrderField*> filled;
    std::vector<const CThostFtdcOrderField*> unfilled;
    for (const auto& order : orders_copy) {
        bool is_fully_filled =
            (order.OrderStatus == THOST_FTDC_OST_AllTraded) && (order.VolumeTraded == order.VolumeTotalOriginal);
        if (is_fully_filled) {
            filled.push_back(&order);
        }
        else {
            unfilled.push_back(&order);
        }
    }
    // 打印函数
    auto printGroup = [](const std::string& title,
        const std::vector<const CThostFtdcOrderField*>& group) {
            if (group.empty()) {
                std::cout << "\n" << title << "（共 0 笔）\n";
                return;
            }
            std::cout << "\n" << title << "（共 " << group.size() << " 笔）\n";
            std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
            std::cout << std::left
                << std::setw(15) << "OrderRef"
                << std::setw(12) << "合约"
                << std::setw(6) << "方向"
                << std::setw(6) << "开平"
                << std::setw(10) << "价格"
                << std::setw(8) << "报单"
                << std::setw(8) << "成交"
                << std::setw(12) << "状态"
                << std::setw(10) << "委托时间"
                << "\n";
            std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
            for (const auto* o : group) {
                std::string ref = o->OrderRef ? o->OrderRef : "";
                std::string inst = o->InstrumentID ? o->InstrumentID : "";
                char dir = (o->Direction == THOST_FTDC_D_Buy) ? 'B' : 'S';
                char offset = o->CombOffsetFlag[0]; // 通常第一个字符有效
                double price = o->LimitPrice;
                int total = o->VolumeTotalOriginal;
                int traded = o->VolumeTraded;

                std::string status;
                switch (o->OrderStatus) {
                case THOST_FTDC_OST_AllTraded:      status = "全部成交"; break;
                case THOST_FTDC_OST_PartTradedQueueing: status = "部分成交还在队列中"; break;
                case THOST_FTDC_OST_PartTradedNotQueueing: status = "部分成交不在队列中"; break;
                case THOST_FTDC_OST_NoTradeQueueing: status = "未成交还在队列中"; break;
                case THOST_FTDC_OST_NoTradeNotQueueing:    status = "未成交不在队列中"; break;
                case THOST_FTDC_OST_Canceled:       status = "已撤单"; break;
                default:                            status = "其他(" + std::to_string(o->OrderStatus) + ")";
                }
                std::string insert_time = std::string(o->InsertDate ? o->InsertDate : "") + " " + std::string(o->InsertTime ? o->InsertTime : "");
                std::string note;
                if (o->OrderStatus == THOST_FTDC_OST_NoTradeNotQueueing && o->StatusMsg) { note = o->StatusMsg; }

                std::cout << std::left
                    << std::setw(15) << ref
                    << std::setw(12) << inst
                    << std::setw(6) << dir
                    << std::setw(6) << offset
                    << std::setw(10) << price
                    << std::setw(8) << total
                    << std::setw(8) << traded
                    << std::setw(12) << status
                    << std::setw(10) << insert_time
                    << note << "\n";
            }
        };

    // 先打印完全成交
    printGroup("完全成交的委托", filled);
    // 再打印未完全成交（含部分成交、未成交、已撤、拒单等）
    printGroup("未完全成交的委托", unfilled);

    std::cout << "\n";
}
/// 查询当日委托回调函数
void CustTpi::OnRspQryOrder(CThostFtdcOrderField* pOrder, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
    bool has_error = (pRspInfo && pRspInfo->ErrorID != 0);
    // 全程持有 m_todayOrdersMutex，保证数据更新和状态通知的原子性
    {
        std::lock_guard<std::mutex> lock(m_todayOrdersMutex);
        if (has_error) {
            std::ostringstream oss;
            oss << "【委托查询失败】错误码: " << pRspInfo->ErrorID << ", 错误信息: " << (pRspInfo->ErrorMsg ? pRspInfo->ErrorMsg : "Unknown");
            Logger::instance().error(oss.str()); // 假设你有 Logger，没有可用 std::cerr
            // 即使出错，如果是最后一包，也要通知等待者，防止死锁
            if (bIsLast) {
                m_order_query_done.store(true);
                m_order_wait_cv.notify_all();
            }
            return;
        }

        // 解析并更新数据
        if (pOrder) {
            // 深拷贝到 vector
            today_orders.push_back(*pOrder);
        }

        // 如果是最后一包，通知等待线程
        if (bIsLast) {
            std::ostringstream oss;
            oss << "【委托查询完成】共收到 " << today_orders.size() << " 笔委托数据。";
            Logger::instance().info(oss.str());

            // 标记完成并唤醒
            m_order_query_done.store(true);
            m_order_wait_cv.notify_all();
        }
    }
    // 锁在此处释放
}
/// 同步委托查询信号
bool CustTpi::waitForOrderQuery() {
    std::unique_lock<std::mutex> lock(m_todayOrdersMutex);
    // 使用 wait_for 增加健壮性，防止网络问题导致永久阻塞
    bool success = m_order_wait_cv.wait_for(lock, std::chrono::seconds(10),
        [this]() {
            return m_order_query_done.load();
        });
    if (!success) {
        Logger::instance().error("【警告】委托查询超时！未在规定时间10s 内收到完整数据。");
        return false;
    }
    // 查询成功（或失败但已结束），重置状态以便下次使用
    // 注意：这里重置为 false，下次调用 getAllOrder 时会再次确认清空，逻辑上是安全的
    m_order_query_done.store(false);
    return true;
}
/// 提供获取投资者委托的接口(避免直接访问私有成员)，使用前需要先调用getAllOrder更新today_orders
std::vector<CThostFtdcOrderField> CustTpi::getOrderVector() const {
    std::lock_guard<std::mutex> lock(m_todayOrdersMutex);
    return today_orders;
}




// 查询当日所有委托(报单+撤单)笔数
int CustTpi::getTodayOrderInsertCount() {
    return static_cast<int>(today_orders.size());
}
// 查询当日撤单笔数
int CustTpi::getTodayOrderCanceledCount() {
    // 撤单数量
    return std::count_if(today_orders.begin(), today_orders.end(),
        [](const CThostFtdcOrderField& o) {
            return o.OrderStatus == THOST_FTDC_OST_Canceled;
        });
}
// 设置阈值的接口
void CustTpi::setOrderInsertThreshold(int threshold) {
    m_order_insert_threshold = threshold;
    std::ostringstream oss;
    oss << "[INFO] 报单笔数阈值已设置为: " << threshold << std::endl;
    Logger::instance().info(oss.str());
}
void CustTpi::setOrderActionThreshold(int threshold) {
    m_order_action_threshold = threshold;
    std::ostringstream oss;
    oss << "[INFO] 撤单笔数阈值已设置为: " << threshold << std::endl;
    Logger::instance().info(oss.str());
}
// 检查本次下单是否会导致报单超限（基于当前today_orders）
bool CustTpi::checkAndWarnOnInsert(){
    bool warned = false;
    // 1. 模拟总委托数 +1
    int new_total = static_cast<int>(today_orders.size()) + 1;
    if (new_total >= m_order_insert_threshold) {
        std::cout << "\n【风控告警】本次下单将使总委托数达到 " << new_total  << "，超过阈值 " << m_order_insert_threshold << "！\n";
        warned = true;
    }
    return warned;
}
// 检查本次撤单是否会导致撤单超限
bool CustTpi::checkAndWarnOnAction() {
    bool warned = false;
    int current_canceled = std::count_if(today_orders.begin(), today_orders.end(),
        [](const CThostFtdcOrderField& o) {
            return o.OrderStatus == THOST_FTDC_OST_Canceled;
        });
    int new_canceled = current_canceled + 1;
    if (new_canceled >= m_order_action_threshold) {
        std::cout << "\n【风控告警】本次撤单将使撤单总数达到 " << new_canceled << "，超过阈值 " << m_order_action_threshold << "！\n";
        warned = true;
    }
    return warned;
}





//查询资金账户响应
void CustTpi::getTradingAccount() {
    // 1. 加锁：重置状态
    {
        std::lock_guard<std::mutex> lock(m_AccountMutex);
        // 可选：手动清零结构体，确保如果失败，获取到的是空数据
        memset(&today_account, 0, sizeof(CThostFtdcTradingAccountField));
        m_account_query_done.store(false);
    }
    // 2. 构造请求
    CThostFtdcQryTradingAccountField req = { 0 };
    strncpy(req.BrokerID, BROKER_ID.c_str(), sizeof(req.BrokerID) - 1);
    strncpy(req.InvestorID, InvestorID.c_str(), sizeof(req.InvestorID) - 1);
    strncpy(req.CurrencyID, CurrencyID.c_str(), sizeof(req.CurrencyID) - 1);
    // 3. 发送
    int ret = m_pUserApi->ReqQryTradingAccount(&req, nRequestID++);
    if (ret != 0) {
        // 发送失败：直接标记完成，唤醒主线程
        std::lock_guard<std::mutex> lock(m_AccountMutex);
        m_account_query_done.store(true);
        m_account_wait_cv.notify_all();
        std::cerr << "【错误】发送资金账户查询请求失败，错误码：" << ret << std::endl;
    }
}
//查询资金账户响应回调函数
void CustTpi::OnRspQryTradingAccount(CThostFtdcTradingAccountField* pTradingAccount,CThostFtdcRspInfoField* pRspInfo,int nRequestID, bool bIsLast) {
    bool has_error = (pRspInfo && pRspInfo->ErrorID != 0);
    {
        std::lock_guard<std::mutex> lock(m_AccountMutex);
        if (has_error) {
            std::cerr << "【资金查询失败】ErrorID=" << pRspInfo->ErrorID << ", Msg=" << (pRspInfo->ErrorMsg ? pRspInfo->ErrorMsg : "") << std::endl;
            // 出错时不更新 today_account，保持为空/旧值
        }
        else if (pTradingAccount) {
            // 只有无错且有数据时才拷贝
            today_account = *pTradingAccount;
            cout << "经纪公司代码BrokerID:" << pTradingAccount->BrokerID << endl;
            cout << "投资者帐号AccountID:" << pTradingAccount->AccountID << endl;
            cout << "上次占用保证金PreMargin:" << pTradingAccount->PreMargin << endl;
            cout << "入金金额Deposit:" << pTradingAccount->Deposit << endl;
            cout << "出金金额Withdraw:" << pTradingAccount->Withdraw << endl;
            cout << "冻结保证金FrozenMargin:" << pTradingAccount->FrozenMargin << endl;
            cout << "冻结资金FrozenCash:" << pTradingAccount->FrozenCash << endl;
            cout << "冻结手续费FrozenCommission:" << pTradingAccount->FrozenCommission << endl;
            cout << "手续费Commission:" << pTradingAccount->Commission << endl;
            cout << "平仓盈亏CloseProfit:" << pTradingAccount->CloseProfit << endl;
            cout << "持仓盈亏PositionProfit:" << pTradingAccount->PositionProfit << endl;
            cout << "可用资金Available:" << pTradingAccount->Available << endl;
            cout << "可取资金WithdrawQuota:" << pTradingAccount->WithdrawQuota << endl;
            cout << "交易日TradingDay:" << pTradingAccount->TradingDay << endl;
            cout << "结算编号SettlementID:" << pTradingAccount->SettlementID << endl;
            cout << "期权市值OptionValue:" << pTradingAccount->OptionValue << endl;
        }
        // 只要是最后一包（无论成功失败），都标记完成并唤醒
        if (bIsLast) {
            m_account_query_done.store(true);
            m_account_wait_cv.notify_all();
        }
    }
}
/// 同步资金账号查询信号
bool CustTpi::waitForAccountQuery() {
    std::unique_lock<std::mutex> lock(m_AccountMutex);
    // 等待标志位变为 true
    bool finished = m_account_wait_cv.wait_for(lock, std::chrono::seconds(10),
        [this]() {
            return m_account_query_done.load();
        });
    if (!finished) {
        std::cerr << "【警告】资金账户查询超时！" << std::endl;
        return false; // 超时返回 false
    }
    // 重置标志位，供下次使用
    m_account_query_done.store(false);
    // 注意：这里返回 true 仅代表“收到了回调”，不代表“业务成功”。
    // 业务是否成功，由调用者检查数据内容决定（见下文 getTradingAccount 或调用处）
    return true;
}
// 获取副本
CThostFtdcTradingAccountField CustTpi::getTradingAccount() const {
    std::lock_guard<std::mutex> lock(m_AccountMutex);
    return today_account;
}





///报单录入请求 录入错误时对应响应OnRspOrderInsert、OnErrRtnOrderInsert，正确时对应回报OnRtnOrder、OnRtnTrade。
void CustTpi::orderInsertWithTracking(const std::string& orderRef,char* InstrumentID, char* ExchangeID, char Direction, char CombOffsetFlag, double LimitPrice, int VolumeTotalOriginal) {
    // 1. 安全检查
    if (!InstrumentID || strlen(InstrumentID) == 0) {
        std::cerr << "【错误】合约代码为空，拒绝下单！" << std::endl;
        return;
    }
    if (VolumeTotalOriginal <= 0) {
        std::cerr << "【错误】委托手数必须大于0！" << std::endl;
        return;
    }
    // 3. 构造 CTP 报单请求结构体
    CThostFtdcInputOrderField pInputOrder = { 0 }; // 初始化为0（重要！）
    strcpy(pInputOrder.BrokerID, BROKER_ID.c_str());
    strcpy(pInputOrder.InvestorID, InvestorID.c_str());
    strcpy(pInputOrder.InstrumentID, InstrumentID);
    if (ExchangeID && strlen(ExchangeID) > 0) {
        strcpy(pInputOrder.ExchangeID, ExchangeID);
    }
    strcpy(pInputOrder.UserID, USER_ID.c_str());
    strcpy(pInputOrder.OrderRef, orderRef.c_str()); // ：用于回调匹配
    pInputOrder.OrderPriceType = THOST_FTDC_OPT_LimitPrice;
    pInputOrder.Direction = Direction;
    pInputOrder.CombOffsetFlag[0] = CombOffsetFlag;
    pInputOrder.CombHedgeFlag[0] = THOST_FTDC_HF_Speculation;
    pInputOrder.LimitPrice = LimitPrice;
    pInputOrder.VolumeTotalOriginal = VolumeTotalOriginal;
    pInputOrder.TimeCondition = THOST_FTDC_TC_GFD;      // 当日有效
    pInputOrder.VolumeCondition = THOST_FTDC_VC_AV;     // 任意数量
    pInputOrder.MinVolume = 1;
    pInputOrder.ContingentCondition = THOST_FTDC_CC_Immediately;
    pInputOrder.StopPrice = 0;
    pInputOrder.ForceCloseReason = THOST_FTDC_FCC_NotForceClose;
    pInputOrder.IsAutoSuspend = 0;
    pInputOrder.UserForceClose = 0;
    // 4. 发送报单请求
    int res = m_pUserApi->ReqOrderInsert(&pInputOrder, nRequestID++);
    // 5. 日志输出
    std::ostringstream oss;
    oss << "发起委托 | OrderRef=" << orderRef
        << " | 合约=" << InstrumentID
        << " | 方向=" << (Direction == THOST_FTDC_D_Buy ? "买" : "卖")
        << " | 开平=" << (CombOffsetFlag == THOST_FTDC_OF_Open ? "开" : "平")
        << " | 价格=" << LimitPrice
        << " | 手数=" << VolumeTotalOriginal
        << " | 状态=" << (res == 0 ? " (成功)" : " (失败)") << std::endl;
    Logger::instance().trade(oss.str());
}
///报单录入请求失败回调函数
void CustTpi::OnRspOrderInsert(CThostFtdcInputOrderField* pInputOrder,CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
    std::string errorMsg = pRspInfo ? convertCTPString(pRspInfo->ErrorMsg) : "未知错误";
    std::string orderRef = pInputOrder ? std::string(pInputOrder->OrderRef) : "NULL";
    std::ostringstream oss;
    oss << "报单失败"
        << "【OnRspOrderInsert】: " << (pRspInfo ? convertCTPString(pRspInfo->ErrorMsg) : "OK")
        << "OrderRef = " << orderRef << ", 错误: " << errorMsg << std::endl;
    Logger::instance().trade(oss.str());
}
///报单录入错误回报回调函数
void CustTpi::OnErrRtnOrderInsert(CThostFtdcInputOrderField* pInputOrder,CThostFtdcRspInfoField* pRspInfo) {
    std::string errorMsg = pRspInfo ? convertCTPString(pRspInfo->ErrorMsg) : "未知错误";
    std::string orderRef = pInputOrder ? std::string(pInputOrder->OrderRef) : "NULL";
    std::ostringstream oss;
    oss << "【OnErrRtnOrderInsert】: " << (pRspInfo ? convertCTPString(pRspInfo->ErrorMsg) : "Unknown error")
        << "【OnErrRtnOrderInsert】OrderRef=" << orderRef << ", 错误: " << errorMsg << std::endl;
    Logger::instance().trade(oss.str());
}
///报单录入请求成功回调函数
void CustTpi::OnRtnOrder(CThostFtdcOrderField* pOrder) {
    std::string orderRef(pOrder->OrderRef);
    std::ostringstream oss;
    oss << "委托回报【OnRtnOrder】 | OrderRef=" << pOrder->OrderRef
        << " | 资金账号=" << pOrder->AccountID
        << " | 委托时间=" << pOrder->InsertTime
        << " | 合约=" << pOrder->InstrumentID
        << " | 方向=" << (pOrder->Direction == THOST_FTDC_D_Buy ? "买" : "卖")
        << " | 开平=" << pOrder->CombOffsetFlag[0]
        << " | 价格=" << pOrder->LimitPrice
        << " | 手数=" << pOrder->VolumeTotalOriginal
        << " | 已成交手数= " << pOrder->VolumeTraded
        << " | 报单提交状态= " << pOrder->OrderSubmitStatus
        << " | 报单编号=" << pOrder->OrderSysID
        << " | 报单状态=" << pOrder->OrderStatus
        << " | 结算编号= " << pOrder->SettlementID
        << " | 状态=" << pOrder->OrderStatus 
        << " | 状态信息=" << convertCTPString(pOrder->StatusMsg)
        << " | 激活时间=" << pOrder->ActiveTime
        << " | 挂起时间=" << pOrder->SuspendTime
        << " | 最后修改时间=" << pOrder->UpdateTime
        << " | 撤销时间=" << pOrder->CancelTime  
        << "\n"
        << "报单状态: 0->已经提交; 1->撤单已经提交; 2->修改已经提交; 3->已经接受;4->报单已经被拒绝;5->撤单已经被拒绝;6->改单已经被拒绝";
    Logger::instance().trade(oss.str());
}
///报单录入请求成功回调函数
void CustTpi::OnRtnTrade(CThostFtdcTradeField* pTrade) {
    std::string orderRef(pTrade->OrderRef);
    std::ostringstream oss;
    oss << "成交回报【OnRtnTrade】 | OrderRef=" << pTrade->OrderRef
        << " | 合约=" << pTrade->InstrumentID
        << " | 成交价=" << pTrade->Price
        << " | 成交手数=" << pTrade->Volume
        << " | 方向=" << (pTrade->Direction == THOST_FTDC_D_Buy ? "买" : "卖");
    Logger::instance().trade(oss.str());
}



/// 撤单录入请求 错误响应: OnRspOrderAction，OnErrRtnOrderAction 正确响应：OnRtnOrder
void CustTpi::orderCancelWithTracking(const std::string& orderRef) {
    if (orderRef.empty()) {
        Logger::instance().trade("【撤单失败】OrderRef 为空\n");
        return;
    }
    // 在 today_orders 中查找匹配的 OrderRef
    CThostFtdcOrderField* foundOrder = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_todayOrdersMutex);
        for (auto& order : today_orders) {
            if (order.OrderRef && orderRef == order.OrderRef) {
                foundOrder = &order;
                break;
            }
        }
    }
    if (!foundOrder) {
        std::ostringstream oss;
        oss << "【撤单失败】OrderRef '" << orderRef << "' 未在当日委托中找到\n";
        Logger::instance().trade(oss.str());
        return;
    }

    // 检查状态是否可撤（注意：这里的状态来自查询结果，可能略滞后）
    char status = foundOrder->OrderStatus;
    bool isActive = (status == THOST_FTDC_OST_NoTradeQueueing) ||
        (status == THOST_FTDC_OST_PartTradedQueueing) ||
        (status == THOST_FTDC_OST_Touched);   

    if (!isActive) {
        std::ostringstream oss;
        oss << "【撤单跳过】OrderRef '" << orderRef
            << "' 状态不可撤（当前状态码=" << static_cast<int>(status) << "）\n";
        Logger::instance().trade(oss.str());
        return;
    }

    // 构造撤单请求
    CThostFtdcInputOrderActionField req = { 0 };
    strcpy(req.BrokerID, BROKER_ID.c_str());
    strcpy(req.InvestorID, InvestorID.c_str());
    strcpy(req.UserID, USER_ID.c_str());

    req.FrontID = foundOrder->FrontID;
    req.SessionID = foundOrder->SessionID;
    strcpy(req.OrderRef, foundOrder->OrderRef);
    strcpy(req.InstrumentID, foundOrder->InstrumentID);
    req.ActionFlag = THOST_FTDC_AF_Delete;

    int ret = m_pUserApi->ReqOrderAction(&req, nRequestID++);
    std::ostringstream oss;
    oss << "【撤单请求】OrderRef=" << orderRef
        << ", 合约=" << foundOrder->InstrumentID
        << ", 返回码=" << ret
        << (ret == 0 ? " (成功)" : " (失败)") << std::endl;
    Logger::instance().trade(oss.str());
}
/// 撤单录入失败 回调函数 请求发送失败（如网络、字段错误）
void CustTpi::OnRspOrderAction(CThostFtdcInputOrderActionField* pInputOrderAction,CThostFtdcRspInfoField* pRspInfo,int nRequestID,bool bIsLast) {
    std::string errorMsg = pRspInfo ? pRspInfo->ErrorMsg : "未知错误";
    std::string orderRef = pInputOrderAction ? pInputOrderAction->OrderRef : "";
    std::ostringstream oss;
    oss << "【OnRspOrderAction】撤单请求失败: " << errorMsg << std::endl;
    Logger::instance().trade(oss.str());
}
// 撤单录入失败 回调函数 撤单被交易所/柜台拒绝
void CustTpi::OnErrRtnOrderAction(CThostFtdcOrderActionField* pOrderAction,CThostFtdcRspInfoField* pRspInfo) {
    std::string errorMsg = pRspInfo ? pRspInfo->ErrorMsg : "未知错误";
    std::string orderRef = pOrderAction ? pOrderAction->OrderRef : "";
    /*std::cout << "【OnErrRtnOrderAction】撤单被拒: " << errorMsg << std::endl;*/
    std::ostringstream oss;
    oss << "【OnErrRtnOrderAction】撤单被拒: " << convertCTPString(errorMsg.c_str()) << std::endl;
    Logger::instance().trade(oss.str());
}
// 撤销所有还在队列中的委托【全撤】
// 注意：此函数应在调用前已执行 getAllOrder + waitForOrderQuery
// 否则 m_today_orders 可能不是最新
void CustTpi::cancelAllUntradedOrders() {
    Logger::instance().trade("\n【正在撤销所有还在队列中的委托...】\n");
    std::vector<std::string> activeOrderRefs;
    {
        std::lock_guard<std::mutex> lock(m_todayOrdersMutex);
        for (const auto& order : today_orders) {
            bool isActive =
                (order.OrderStatus == THOST_FTDC_OST_NoTradeQueueing) ||
                (order.OrderStatus == THOST_FTDC_OST_PartTradedQueueing) ||
                (order.OrderStatus == THOST_FTDC_OST_Touched);
            if (isActive && order.OrderRef) {
                activeOrderRefs.emplace_back(order.OrderRef);
            }
        }
    }
    if (activeOrderRefs.empty()) {
        Logger::instance().trade("无符合条件的未成交委托。\n");
        return;
    }
    int cancel_count = 0;
    for (const auto& orderRef : activeOrderRefs) {
        orderCancelWithTracking(orderRef);
        cancel_count++;
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // 避免限频
    }
    std::ostringstream oss;
    oss << "已提交 " << cancel_count << " 笔撤单请求。\n";
    Logger::instance().trade(oss.str());
}































