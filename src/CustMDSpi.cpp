#include "CustMDSpi.h"
#include <iostream>
#include <cstring>

void CustMDSpi::set_Subscribe_Instrument_Vector_vector(vector<string> p) {
    Subscribe_Instrument_Vector = std::move(p);
}

void CustMDSpi::connect() {
    std::cout << "in connect" << std::endl;
    fs::create_directories("./flow_md");
    m_mdApi = CThostFtdcMdApi::CreateFtdcMdApi("./flow_md/", true, true);
    m_mdApi->RegisterSpi(this);
    m_mdApi->RegisterFront(const_cast<char*>(MDFront.c_str()));
    m_mdApi->Init();
    std::cout << "行情API初始化中..." << std::endl;
}

void CustMDSpi::OnFrontConnected() {
    std::cout << "行情前置已连接，正在登录..." << std::endl;
    CThostFtdcReqUserLoginField login = { 0 };
    strcpy(login.BrokerID, BROKER_ID.c_str());
    strcpy(login.UserID, USER_ID.c_str());
    strcpy(login.Password, USER_PAW.c_str());
    m_mdApi->ReqUserLogin(&login, 1);
}

void CustMDSpi::OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin,
    CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
    if (pRspInfo && pRspInfo->ErrorID == 0) {
        std::cout << "行情登录成功!交易日: " << pRspUserLogin->TradingDay << std::endl;
        subscribe();
    }
    else {
        std::cout << "行情登录失败: " << (pRspInfo ? pRspInfo->ErrorMsg : "Unknown") << std::endl;
    }
}

void CustMDSpi::subscribe() {
    if (Subscribe_Instrument_Vector.empty()) return;
    vector<const char*> instruments;
    for (const auto& ins : Subscribe_Instrument_Vector) {
        instruments.push_back(ins.c_str());
    }
    int ret = m_mdApi->SubscribeMarketData(
        const_cast<char**>(instruments.data()),
        static_cast<int>(instruments.size())
    );
    std::cout << "订阅行情结果: " << (ret == 0 ? "成功¦" : "失败") << std::endl;
}

void CustMDSpi::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* pDepthMarketData) {
    std::cout << "[" << pDepthMarketData->InstrumentID << "] "
        << "Last=" << pDepthMarketData->LastPrice
        << " Bid=" << pDepthMarketData->BidPrice1 << "(" << pDepthMarketData->BidVolume1 << ")"
        << " Ask=" << pDepthMarketData->AskPrice1 << "(" << pDepthMarketData->AskVolume1 << ")"
        << std::endl;

    // 更新最新行情（线程安全）
    std::string inst(pDepthMarketData->InstrumentID);
    std::unique_lock<std::shared_mutex> lock(m_tickMutex);
    m_latestTicks[inst] = *pDepthMarketData; // 深拷贝结构体
}

void CustMDSpi::OnFrontDisconnected(int nReason) {
    std::cout << "行情前置断开, reason=" << nReason << std::endl;
}
