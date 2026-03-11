#include "CustTpi.h"
#include "CustMDSpi.h"
#include "ConvertEncoding.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <map>
#include <unordered_map>
#include <string>
#include <vector>
#include <filesystem>
#include <limits> // 用于清理输入流
#include <atomic>
#include <cmath>  
// 全局原子计数器，确保高并发下 ID 唯一
std::atomic<uint64_t> g_order_ref_counter{ 0 };


namespace fs = std::filesystem;

// 全局交易实例
CustTpi tpi;

// 全局同步变量 (保持原有逻辑)
std::mutex g_mutex;
std::condition_variable g_cv;
bool g_ready = false;
std::atomic<bool> g_running{ true };

// 信号处理 (保持原有逻辑)
void signal_handler(int sig) {
    std::cout << "\n收到退出信号，正在关闭...\n";
    g_running = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_ready = true;
    }
    g_cv.notify_all();
}

// 等待事件就绪 (保持原有逻辑)
void wait_for_ready() {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_cv.wait(lock, [] { return g_ready; });
    g_ready = false;
}

// 生成订单引用 (保持原有逻辑)
std::string generateOrderRef() {
    // 直接返回递增的数字字符串
    // 例如：1, 2, 3, ... 1000, 1001
    // 这样绝对满足“严格递增”要求，且全是数字
    uint64_t old_val = g_order_ref_counter.fetch_add(1, std::memory_order_relaxed);
    uint64_t new_val = old_val + 1; // 强制从 1 开始

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(new_val));
    return std::string(buffer);
}

// 下单函数
void interactiveOrder() {
    std::cout << "=== 下单系统 ===" << std::endl;
    std::string instrument;
    std::string exchange;
    std::string directionStr;
    std::string offsetStr;
    double price;
    int volume;

    std::cout << "请输入合约代码（如 ag2604）: ";
    std::getline(std::cin, instrument);

    std::cout << "请输入交易所（如 SHFE）: ";
    std::getline(std::cin, exchange);

    std::cout << "请输入方向（Buy 或 Sell）: ";
    std::getline(std::cin, directionStr);

    // 将 directionStr 全部转换为小写方便识别
    std::transform(directionStr.begin(), directionStr.end(), directionStr.begin(),
        [](unsigned char c) { return std::tolower(c); });

    std::cout << "请输入开平标志（Open 或 Close）: ";
    std::getline(std::cin, offsetStr);
    std::transform(offsetStr.begin(), offsetStr.end(), offsetStr.begin(),
        [](unsigned char c) { return std::tolower(c); });

    std::cout << "请输入价格: ";
    std::cin >> price;

    std::cout << "请输入手数: ";
    std::cin >> volume;

    // 清理缓冲区，防止影响后续 cin >> confirm
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    // 方向转换
    TThostFtdcDirectionType direction;
    if (directionStr == "buy") {
        direction = THOST_FTDC_D_Buy;
    }
    else if (directionStr == "sell") {
        direction = THOST_FTDC_D_Sell;
    }
    else {
        std::cerr << "错误：方向只能是 Buy 或 Sell" << std::endl;
        return;
    }

    TThostFtdcOffsetFlagType offsetFlag; 

    if (offsetStr == "open") {
        offsetFlag = THOST_FTDC_OF_Open;

        // >>>>>>>>>>>>>> 风控预检流程 <<<<<<<<<<<<<<
        bool has_warning = tpi.checkAndWarnOnInsert();
        if (has_warning) {
            std::cout << "是否仍要提交此委托？(y/n): ";
            char confirm;
            std::cin >> confirm;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            if (confirm != 'y' && confirm != 'Y') {
                std::cout << "已取消下单。\n";
                return;
            }
        }
        // 执行真实下单
        string ref = generateOrderRef();
        std::cout << "当前委托的委托引用为:" << ref << std::endl;

        tpi.orderInsertWithTracking(
            ref,
            const_cast<char*>(instrument.c_str()),
            exchange.empty() ? nullptr : const_cast<char*>(exchange.c_str()),
            direction,
            offsetFlag,
            price,
            volume
        );
    }
    else if (offsetStr == "close") {
        std::cout << "\n【正在查询当前持仓...】\n";
        tpi.getAllPosition();
        tpi.waitForPositionQuery();

        auto h = tpi.getSinglePosition(instrument);

        int availableYd = 0;
        int availableToday = 0;

        if (direction == THOST_FTDC_D_Sell) {
            // 卖出平仓 → 平多头
            availableYd = h.yesterday_buy_volume; // 昨仓=昨天多仓
            availableToday = h.today_buy_volume;  // 今仓=今天多仓
        }
        else if (direction == THOST_FTDC_D_Buy) {
            // 买入平仓 → 平空头
            availableYd = h.yesterday_sell_volume;
            availableToday = h.today_sell_volume;
        }

        int totalAvailable = availableYd + availableToday;
        // 检查本次平仓手数是否超过可用仓位
        if (volume > totalAvailable) {
            std::cerr << "【错误】仓位不足！可用平仓手数为 " << totalAvailable << "，但请求平仓 " << volume << " 手。\n";
            // return;

            std::cout << "是否仍要提交此委托？(y/n): ";
            char confirm;
            std::cin >> confirm;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            if (confirm != 'y' && confirm != 'Y') {
                std::cout << "已取消下单。\n";
                return;
            }
            else {
                string ref = generateOrderRef();
                tpi.orderInsertWithTracking(
                    ref,
                    const_cast<char*>(instrument.c_str()),
                    exchange.empty() ? nullptr : const_cast<char*>(exchange.c_str()),
                    direction,
                    THOST_FTDC_OF_CloseYesterday,
                    price,
                    volume
                );
            }
        }
        // 昨仓 > volume（优先平昨仓 符合国内期货规则）
        else if (availableYd >= volume) {
            std::cout << ">>> 优先使用【平昨】 " << availableYd << " 手" << endl;
            offsetFlag = THOST_FTDC_OF_CloseYesterday;

            // >>>>>>>>>>>>>> 风控预检流程 <<<<<<<<<<<<<<
            bool has_warning = tpi.checkAndWarnOnInsert();
            if (has_warning) {
                std::cout << "是否仍要提交此委托进行【平昨】？(y/n): ";
                char confirm;
                std::cin >> confirm;
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                if (confirm != 'y' && confirm != 'Y') {
                    std::cout << "已取消下单。\n";
                    return;
                }
            }
            // 执行真实下单
            string ref = generateOrderRef();
            std::cout << "当前委托的委托引用为:" << ref << std::endl;

            tpi.orderInsertWithTracking(
                ref,
                const_cast<char*>(instrument.c_str()),
                exchange.empty() ? nullptr : const_cast<char*>(exchange.c_str()),
                direction,
                offsetFlag,
                price,
                volume
            );
        }
        // 0 < 昨仓 < volume，把昨仓全部平掉，再去平今仓
        else if (availableYd < volume && availableYd > 0) {
            int volume_today = volume - availableYd;
            std::cout << ">>> 【平昨】平掉全部昨仓 " << availableYd << " 手，剩下的使用【平今】" << volume_today << "手" << endl;
            offsetFlag = THOST_FTDC_OF_CloseYesterday;
            // >>>>>>>>>>>>>> 风控预检流程 <<<<<<<<<<<<<<
            bool has_warning_Yd = tpi.checkAndWarnOnInsert();
            if (has_warning_Yd) {
                std::cout << "是否仍要提交此委托？(y/n): ";
                char confirm;
                std::cin >> confirm;
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                if (confirm != 'y' && confirm != 'Y') {
                    std::cout << "已取消下单。\n";
                    return;
                }
            }
            // 执行真实下单
            string refYd = generateOrderRef();
            std::cout << "【平昨】委托的委托引用为:" << refYd << std::endl;
            tpi.orderInsertWithTracking(
                refYd,
                const_cast<char*>(instrument.c_str()),
                exchange.empty() ? nullptr : const_cast<char*>(exchange.c_str()),
                direction,
                offsetFlag,
                price,
                availableYd
            );

            // 平今
            offsetFlag = THOST_FTDC_OF_CloseToday;
            // >>>>>>>>>>>>>> 风控预检流程 <<<<<<<<<<<<<<
            bool has_warning = tpi.checkAndWarnOnInsert();
            if (has_warning) {
                std::cout << "是否仍要提交此委托？(y/n): ";
                char confirm;
                std::cin >> confirm;
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                if (confirm != 'y' && confirm != 'Y') {
                    std::cout << "已取消下单。\n";
                    return;
                }
            }
            // 执行真实下单
            string ref = generateOrderRef();
            std::cout << "【平今】委托的委托引用为:" << ref << std::endl;
            tpi.orderInsertWithTracking(
                ref,
                const_cast<char*>(instrument.c_str()),
                exchange.empty() ? nullptr : const_cast<char*>(exchange.c_str()),
                direction,
                offsetFlag,
                price,
                volume_today
            );

        }
        else if (availableYd == 0) {
            std::cout << ">>> 昨仓为0，【平今】 " << volume << "手" << endl;
            // 平今
            offsetFlag = THOST_FTDC_OF_CloseToday;
            // >>>>>>>>>>>>>> 风控预检流程 <<<<<<<<<<<<<<
            bool has_warning = tpi.checkAndWarnOnInsert();
            if (has_warning) {
                std::cout << "是否仍要提交此委托？(y/n): ";
                char confirm;
                std::cin >> confirm;
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                if (confirm != 'y' && confirm != 'Y') {
                    std::cout << "已取消下单。\n";
                    return;
                }
            }
            // 执行真实下单
            string ref = generateOrderRef();
            std::cout << "【平今】委托的委托引用为:" << ref << std::endl;
            tpi.orderInsertWithTracking(
                ref,
                const_cast<char*>(instrument.c_str()),
                exchange.empty() ? nullptr : const_cast<char*>(exchange.c_str()),
                direction,
                offsetFlag,
                price,
                volume
            );
        }
    }
    else {
        std::cerr << "错误：开平标志只能是 Open 或 Close" << std::endl;
        return;
    }
    std::cout << ">>> 委托已提交，等待柜台确认...\n";

    // 刷新委托列表
    std::cout << "【自动刷新委托列表...】\n";
    tpi.getAllOrder();
    tpi.waitForOrderQuery();
    std::cout << "本次下单流程完成。\n";
}

// 撤单函数
void interactiveDelete() {
    std::cout << "===撤单系统 ===" << std::endl;
    // >>>>>>>>>>>>>> 风控预检流程 <<<<<<<<<<<<<<
    bool has_warning = tpi.checkAndWarnOnAction();
    if (has_warning) {
        std::cout << "是否仍要提交此委托？(y/n): ";
        char confirm;
        std::cin >> confirm;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        if (confirm != 'y' && confirm != 'Y') {
            std::cout << "已取消下单。\n";
            return;
        }
    }
    tpi.getAllOrder();
    tpi.waitForOrderQuery();

    std::cout << "\n请输入委托引用进行撤单以测试撤单阈值..." << std::endl;
    std::string ref;
    std::getline(std::cin, ref);
    if (!ref.empty()) {
        tpi.orderCancelWithTracking(ref);
    }
}

// 下单函数，2.4.1. 交易指令检查功能
// 有些期货公司要求我们的系统检查订单合约代码错误、订单价格最小变动价位错误、订单委托数量超单笔最大委托数量而不是上报给CTP让CTP检查，
// 但是有些期货公司却要求上报给CTP检查
// 下单函数，下单之前检查合约代码、订单价格最小变动价格、订单委托数据是否合法,如果期货公司要求我们系统进行检查就调用下面的函数
void interactiveOrderWithCheck() {
    std::cout << "=== 下单系统（包含自己检查合约代码、价格、委托数量等是否合法） ===" << std::endl;
    std::string instrument;
    std::string exchange;
    std::string directionStr;
    std::string offsetStr;
    double price;
    int volume;

    std::cout << "请输入合约代码（如 ag2604）: ";
    std::getline(std::cin, instrument);

    std::cout << "请输入交易所（如 SHFE）: ";
    std::getline(std::cin, exchange);


    // 2.4.1  测试点1：验证订单合约代码错误时，系统能检查出错误并拒绝报单
    const auto& instVector = tpi.getInstrumentVector();
    const CThostFtdcInstrumentField* targetInst = nullptr;
    // 遍历查找匹配的合约 (同时匹配合约代码和交易所，如果交易所为空则只匹配代码)
    for (const auto& inst : instVector) {
        if (inst.InstrumentID == instrument) {
            if (exchange.empty() || inst.ExchangeID == exchange) {
                targetInst = &inst;
                break;
            }
        }
    }
    if (!targetInst) {
        std::cerr << "【订单合约代码错误】未找到合约代码 [" << instrument << "]";
        if (!exchange.empty()) {
            std::cerr << " 在交易所 [" << exchange << "]";
        }
        std::cerr << " 的信息。请检查合约代码是否正确或是否已收盘/退市。\n";
        return;
    }


    std::cout << "请输入方向（Buy 或 Sell）: ";
    std::getline(std::cin, directionStr);

    // 将 directionStr 全部转换为小写方便识别
    std::transform(directionStr.begin(), directionStr.end(), directionStr.begin(),
        [](unsigned char c) { return std::tolower(c); });

    std::cout << "请输入开平标志（Open 或 Close）: ";
    std::getline(std::cin, offsetStr);
    std::transform(offsetStr.begin(), offsetStr.end(), offsetStr.begin(),
        [](unsigned char c) { return std::tolower(c); });

    std::cout << "请输入价格: ";
    std::cin >> price;


    // [2.4.1] 交易指令检查功能  测试点2：验证订单价格最小变动价位错误时，系统能检查出错误并拒绝报单
    double priceTick = targetInst->PriceTick;
    // 使用一个小的 epsilon 处理浮点数精度问题，或者将价格放大后取模
    // 逻辑：(Price / PriceTick) 应该非常接近一个整数
    double ratio = price / priceTick;
    double nearestInt = std::round(ratio);
    if (std::abs(ratio - nearestInt) > 1e-6) {
        std::cerr << "【价格最小变动价位错误】价格不合法！\n";
        std::cerr << "   输入价格: " << price << "\n";
        std::cerr << "   该合约最小变动价位 (Tick): " << priceTick << "\n";
        std::cerr << "   合法的价格必须是 " << priceTick << " 的整数倍 (例如: "
            << (nearestInt * priceTick) << ").\n";
        return;
    }


    std::cout << "请输入手数: ";
    std::cin >> volume;


    // [2.4.1] 交易指令检查功能  测试点3：验证订单委托数量超单笔最大委托数量时，系统能检查出错误并拒绝报单
    if (targetInst->MaxMarketOrderVolume > 0 && volume > targetInst->MaxMarketOrderVolume) {
        std::cerr << "【订单委托数量超单笔最大委托数量】委托手数 (" << volume << ") 超过了该合约定义的参考最大下单量 ("
            << targetInst->MaxMarketOrderVolume << ")。\n";
        return;
    }


    // 清理缓冲区，防止影响后续 cin >> confirm
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    // 方向转换
    TThostFtdcDirectionType direction;
    if (directionStr == "buy") {
        direction = THOST_FTDC_D_Buy;
    }
    else if (directionStr == "sell") {
        direction = THOST_FTDC_D_Sell;
    }
    else {
        std::cerr << "错误：方向只能是 Buy 或 Sell" << std::endl;
        return;
    }

    TThostFtdcOffsetFlagType offsetFlag;

    if (offsetStr == "open") {
        offsetFlag = THOST_FTDC_OF_Open;

        // >>>>>>>>>>>>>> 风控预检流程 <<<<<<<<<<<<<<
        bool has_warning = tpi.checkAndWarnOnInsert();
        if (has_warning) {
            std::cout << "是否仍要提交此委托？(y/n): ";
            char confirm;
            std::cin >> confirm;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            if (confirm != 'y' && confirm != 'Y') {
                std::cout << "已取消下单。\n";
                return;
            }
        }
        // 执行真实下单
        string ref = generateOrderRef();
        std::cout << "当前委托的委托引用为:" << ref << std::endl;

        tpi.orderInsertWithTracking(
            ref,
            const_cast<char*>(instrument.c_str()),
            exchange.empty() ? nullptr : const_cast<char*>(exchange.c_str()),
            direction,
            offsetFlag,
            price,
            volume
        );
    }
    else if (offsetStr == "close") {
        std::cout << "\n【正在查询当前持仓...】\n";
        tpi.getAllPosition();
        tpi.waitForPositionQuery();

        auto h = tpi.getSinglePosition(instrument);

        int availableYd = 0;
        int availableToday = 0;

        if (direction == THOST_FTDC_D_Sell) {
            // 卖出平仓 → 平多头
            availableYd = h.yesterday_buy_volume; // 昨仓=昨天多仓
            availableToday = h.today_buy_volume;  // 今仓=今天多仓
        }
        else if (direction == THOST_FTDC_D_Buy) {
            // 买入平仓 → 平空头
            availableYd = h.yesterday_sell_volume;
            availableToday = h.today_sell_volume;
        }

        int totalAvailable = availableYd + availableToday;
        // 检查本次平仓手数是否超过可用仓位
        if (volume > totalAvailable) {
            std::cerr << "【错误】仓位不足！可用平仓手数为 " << totalAvailable << "，但请求平仓 " << volume << " 手。\n";
            // return;

            std::cout << "是否仍要提交此委托？(y/n): ";
            char confirm;
            std::cin >> confirm;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            if (confirm != 'y' && confirm != 'Y') {
                std::cout << "已取消下单。\n";
                return;
            }
            else {
                string ref = generateOrderRef();
                tpi.orderInsertWithTracking(
                    ref,
                    const_cast<char*>(instrument.c_str()),
                    exchange.empty() ? nullptr : const_cast<char*>(exchange.c_str()),
                    direction,
                    THOST_FTDC_OF_CloseYesterday,
                    price,
                    volume
                );
            }
        }
        // 昨仓 > volume（优先平昨仓 符合国内期货规则）
        else if (availableYd >= volume) {
            std::cout << ">>> 优先使用【平昨】 " << availableYd << " 手" << endl;
            offsetFlag = THOST_FTDC_OF_CloseYesterday;

            // >>>>>>>>>>>>>> 风控预检流程 <<<<<<<<<<<<<<
            bool has_warning = tpi.checkAndWarnOnInsert();
            if (has_warning) {
                std::cout << "是否仍要提交此委托进行【平昨】？(y/n): ";
                char confirm;
                std::cin >> confirm;
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                if (confirm != 'y' && confirm != 'Y') {
                    std::cout << "已取消下单。\n";
                    return;
                }
            }
            // 执行真实下单
            string ref = generateOrderRef();
            std::cout << "当前委托的委托引用为:" << ref << std::endl;

            tpi.orderInsertWithTracking(
                ref,
                const_cast<char*>(instrument.c_str()),
                exchange.empty() ? nullptr : const_cast<char*>(exchange.c_str()),
                direction,
                offsetFlag,
                price,
                volume
            );
        }
        // 0 < 昨仓 < volume，把昨仓全部平掉，再去平今仓
        else if (availableYd < volume && availableYd > 0) {
            int volume_today = volume - availableYd;
            std::cout << ">>> 【平昨】平掉全部昨仓 " << availableYd << " 手，剩下的使用【平今】" << volume_today << "手" << endl;
            offsetFlag = THOST_FTDC_OF_CloseYesterday;
            // >>>>>>>>>>>>>> 风控预检流程 <<<<<<<<<<<<<<
            bool has_warning_Yd = tpi.checkAndWarnOnInsert();
            if (has_warning_Yd) {
                std::cout << "是否仍要提交此委托？(y/n): ";
                char confirm;
                std::cin >> confirm;
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                if (confirm != 'y' && confirm != 'Y') {
                    std::cout << "已取消下单。\n";
                    return;
                }
            }
            // 执行真实下单
            string refYd = generateOrderRef();
            std::cout << "【平昨】委托的委托引用为:" << refYd << std::endl;
            tpi.orderInsertWithTracking(
                refYd,
                const_cast<char*>(instrument.c_str()),
                exchange.empty() ? nullptr : const_cast<char*>(exchange.c_str()),
                direction,
                offsetFlag,
                price,
                availableYd
            );

            // 平今
            offsetFlag = THOST_FTDC_OF_CloseToday;
            // >>>>>>>>>>>>>> 风控预检流程 <<<<<<<<<<<<<<
            bool has_warning = tpi.checkAndWarnOnInsert();
            if (has_warning) {
                std::cout << "是否仍要提交此委托？(y/n): ";
                char confirm;
                std::cin >> confirm;
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                if (confirm != 'y' && confirm != 'Y') {
                    std::cout << "已取消下单。\n";
                    return;
                }
            }
            // 执行真实下单
            string ref = generateOrderRef();
            std::cout << "【平今】委托的委托引用为:" << ref << std::endl;
            tpi.orderInsertWithTracking(
                ref,
                const_cast<char*>(instrument.c_str()),
                exchange.empty() ? nullptr : const_cast<char*>(exchange.c_str()),
                direction,
                offsetFlag,
                price,
                volume_today
            );

        }
        else if (availableYd == 0) {
            std::cout << ">>> 昨仓为0，【平今】 " << volume << "手" << endl;
            // 平今
            offsetFlag = THOST_FTDC_OF_CloseToday;
            // >>>>>>>>>>>>>> 风控预检流程 <<<<<<<<<<<<<<
            bool has_warning = tpi.checkAndWarnOnInsert();
            if (has_warning) {
                std::cout << "是否仍要提交此委托？(y/n): ";
                char confirm;
                std::cin >> confirm;
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                if (confirm != 'y' && confirm != 'Y') {
                    std::cout << "已取消下单。\n";
                    return;
                }
            }
            // 执行真实下单
            string ref = generateOrderRef();
            std::cout << "【平今】委托的委托引用为:" << ref << std::endl;
            tpi.orderInsertWithTracking(
                ref,
                const_cast<char*>(instrument.c_str()),
                exchange.empty() ? nullptr : const_cast<char*>(exchange.c_str()),
                direction,
                offsetFlag,
                price,
                volume
            );
        }
    }
    else {
        std::cerr << "错误：开平标志只能是 Open 或 Close" << std::endl;
        return;
    }
    std::cout << ">>> 委托已提交，等待柜台确认...\n";

    // 刷新委托列表
    std::cout << "【自动刷新委托列表...】\n";
    tpi.getAllOrder();
    tpi.waitForOrderQuery();
    std::cout << "本次下单流程完成。\n";
}

// ============================================================================
// 测试模块函数定义
// ============================================================================

// [2.1.2] 基础交易功能 开仓/平仓测试
void testInsertOrder() {
    std::cout << "\n=== 2.1.2.基础交易功能 测试点 1或2：验证能正常下达开仓或者平仓指令(最好在涨跌停价附近下单方便撤单测试) ===" << std::endl;
    std::cout << "\n[步骤 1] 测试开仓或者平仓指令." << std::endl;
    interactiveOrder(); // 用户需输入 Open或者Close
}

// [2.1.2] 基础交易功能 撤单测试
void testDeleteOrder() {
    std::cout << "\n=== 2.1.2 基础交易功能 测试点 3：验证能正常下达撤单指令 ===" << std::endl;
    interactiveDelete(); // 执行撤单
}

// [2.2.1] 系统连接异常监测功能
void testNetworkStability() {
    std::cout << "\n=== [2.2.1] 系统连接异常监测功能 ===" << std::endl;
    std::cout << "\n测试点1：验证与宏源期货交易信息系统（柜台）连接成功时，能正常显示连接成功" << std::endl;
    std::cout << "\n直接截图最开始的连接成功页面" << std::endl;

    std::cout << "\n测试点2：验证与宏源期货交易信息系统（柜台）连接断开，能正常显示连接断开" << std::endl;
    std::cout << "\n[Windows系统操作指引] 断开网络" << std::endl;
    std::cout << "\n[Linux系统操作指引] 请重新打开一个终端并执行以下命令模拟断网 (需 sudo 权限):" << std::endl;
    std::cout << "sudo iptables -A OUTPUT -d {TradeFrontAddress} -p tcp --dport {TradeFrontPort} -j REJECT --reject-with tcp-reset" << std::endl;
    std::cout << "(请按回车键继续...)" << std::endl;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::cout << "\n等待检测断开连接 (约 60 秒内系统应报错)..." << std::endl;
    std::cout << "(断开后请按回车键继续...)" << std::endl;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::cout << "\n测试点3：验证与宏源期货交易信息系统（柜台）连接断开，能正常显示重连成功" << std::endl;
    std::cout << "\n[Windows系统操作指引] 恢复网络:" << std::endl;
    std::cout << "\n[Linux系统操作指引] 请重新打开一个终端并在终端执行以下命令恢复网络:" << std::endl;
    std::cout << "sudo iptables -D OUTPUT -d {TradeFrontAddress} -p tcp --dport {TradeFrontPort} -j REJECT --reject-with tcp-reset" << std::endl;
    std::cout << "(恢复后按回车键继续...)" << std::endl;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cout << "\n等待重连..." << std::endl;
}

// [2.2.2] 报撤单笔数监测功能
void testStatistics() {
    std::cout << "\n=== [2.2.2] 撤单笔数监测功能 ===" << std::endl;

    std::cout << "\n[刷新数据]查询当日委托中..." << std::endl;
    tpi.getAllOrder();
    tpi.waitForOrderQuery();

    int insertCount = tpi.getTodayOrderInsertCount();
    int cancelCount = tpi.getTodayOrderCanceledCount();

    std::cout << "当前统计结果:" << std::endl;
    std::cout << "  - 今日总报单数：" << insertCount << std::endl;
    std::cout << "  - 今日总撤单数：" << cancelCount << std::endl;
}

// [2.3.1] 阈值设置及预警功能
void testThresholds() {
    std::cout << "\n=== [2.3.1].阈值设置及预警功能 ===" << std::endl;

    // 设置低阈值
    tpi.setOrderInsertThreshold(2);
    tpi.setOrderActionThreshold(1);
    std::cout << "已设置低阈值：报单>2, 撤单>1 触发告警。\n";

    std::cout << "[阶段 1] 请连续报单以触发【报单超阈值】告警(最好在涨跌停附近下单方便第二阶段撤单)..." << std::endl;
    // 循环下单阶段
    char continueChoice = 'y';
    while (continueChoice == 'y' || continueChoice == 'Y') {
        interactiveOrder(); // 执行下单

        // 询问是否继续
        std::cout << "\n是否继续下一笔委托以观察告警？(y/n): ";
        std::cin >> continueChoice;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // 清理缓冲区
    }

    std::cout << "[阶段 2] 请连续撤单以触发【撤单超阈值】告警..." << std::endl;
    char continueDeleteChoice = 'y';
    while (continueDeleteChoice == 'y' || continueDeleteChoice == 'Y') {
        interactiveDelete(); // 执行撤单

        std::cout << "\n是否继续撤单一笔以观察告警？(y/n): ";
        std::cin >> continueDeleteChoice;
    }
    std::cout << ">>> 阈值测试结束，请检查控制台是否有告警输出。\n" << std::endl;
}

// [2.4.1] 交易指令检查功能  测试点1：验证订单合约代码错误时，系统能检查出错误并拒绝报单
void testCodeError() {
    std::cout << "\n=== [2.4.1] 交易指令检查功能 ===" << std::endl;

    std::cout << "\n测试点1：验证订单合约代码错误时，系统能检查出错误并拒绝报单:" << std::endl;
    std::cout << "\n进入报单系统，报单时请输入一个已退市或者不存在的合约(如 ag1901)" << std::endl;
    //interactiveOrder();    // 不检查，直接上报给CTP，让CTP返回错误信息

    // 有些期货公司要求我们的系统检查订单合约代码错误、订单价格最小变动价位错误、订单委托数量超单笔最大委托数量而不是上报给CTP，
    // 但是有些期货公司却要求上报给CTP检查让CTP返回报错信息
    // 下单函数，下单之前检查合约代码、订单价格最小变动价格、订单委托数据是否合法,如果期货公司要求我们系统进行检查就调用下面的函数
    interactiveOrderWithCheck();

    std::cout << "\n>>> 请检查上述操作是否被系统拦截或返回了正确的错误码。\n" << std::endl;
}

// [2.4.1] 交易指令检查功能  测试点2：验证订单价格最小变动价位错误时，系统能检查出错误并拒绝报单
void testPriceError() {
    std::cout << "\n=== [2.4.1] 交易指令检查功能 ===" << std::endl;

    std::cout << "\n测试点2：验证订单价格最小变动价位错误时，系统能检查出错误并拒绝报单:" << std::endl;
    std::cout << "\n[测试 2] 请输入一个【价格不符合最小变动价位】的订单（比如ag2605报价19000.05）:" << std::endl;
    //interactiveOrder();
    
    // 有些期货公司要求我们的系统检查订单合约代码错误、订单价格最小变动价位错误、订单委托数量超单笔最大委托数量而不是上报给CTP，
    // 但是有些期货公司却要求上报给CTP检查让CTP返回报错信息
    // 下单函数，下单之前检查合约代码、订单价格最小变动价格、订单委托数据是否合法,如果期货公司要求我们系统进行检查就调用下面的函数
    interactiveOrderWithCheck();

    std::cout << "\n>>> 请检查上述操作是否被系统拦截或返回了正确的错误码。\n" << std::endl;
}

// [2.4.1] 交易指令检查功能  测试点3：验证订单委托数量超单笔最大委托数量时，系统能检查出错误并拒绝报单
void testVolumeError() {
    std::cout << "\n=== [2.4.1] 交易指令检查功能 ===" << std::endl;

    std::cout << "\n测试点3：验证订单委托数量超单笔最大委托数量时，系统能检查出错误并拒绝报单:" << std::endl;
    std::cout << "\n[测试 3] 请输入一个【超大手数】的订单 (比如v2605市价限仓1000手，委托1001手(注意超大手数不能导致账号可用资金不足，否则柜台会报资金不足，导致不通过该测试)):" << std::endl;
    // interactiveOrder();

    // 有些期货公司要求我们的系统检查订单合约代码错误、订单价格最小变动价位错误、订单委托数量超单笔最大委托数量而不是上报给CTP，
    // 但是有些期货公司却要求上报给CTP检查让CTP返回报错信息
    // 下单函数，下单之前检查合约代码、订单价格最小变动价格、订单委托数据是否合法,如果期货公司要求我们系统进行检查就调用下面的函数
    interactiveOrderWithCheck();

    std::cout << "\n>>> 请检查上述操作是否被系统拦截或返回了正确的错误码。\n" << std::endl;
}

// [2.4.2] 错误提示功能  测试点1：验证系统能正常接收并展示柜台返回的资金不足错误码
void testInsufficientFunds() {
    std::cout << "\n=== [2.4.2] 错误提示功能 ===" << std::endl;

    std::cout << "\n测试点1：验证系统能正常接收并展示柜台返回的资金不足错误码" << std::endl;
    std::cout << "\n[测试1] 请输入一个【超大手数】的订单 导致账号可用资金不足:" << std::endl;
    interactiveOrder();

    std::cout << "\n>>> 请检查上述操作是否被系统拦截或返回了正确的错误码。\n" << std::endl;
}

// [2.4.2] 错误提示功能  测试点2：验证系统能正常接收并展示柜台返回的持仓不足错误码
void testInsufficientPosition() {
    std::cout << "\n=== [2.4.2] 错误提示功能 ===" << std::endl;

    std::cout << "\n测试点2：验证系统能正常接收并展示柜台返回的持仓不足错误码" << std::endl;
    std::cout << "\n[测试2] 请平仓一个超大手数的订单 导致账号可用持仓不足（比如持仓3手ag2604,报单时平仓4手ag2604）:" << std::endl;
    interactiveOrder();

    std::cout << "\n>>> 请检查上述操作是否被系统拦截或返回了正确的错误码。\n" << std::endl;
}

// [2.4.2] 错误提示功能  测试点3：验证系统能正常接收并展示柜台返回的市场状态错误码
void testTimeError() {
    std::cout << "\n=== [2.4.2] 错误提示功能 ===" << std::endl;

    std::cout << "\n测试点3：验证系统能正常接收并展示柜台返回的市场状态错误码" << std::endl;
    std::cout << "\n[测试3] 请在市场休市时间（日盘10:15-10:30之间）进行报单该时间不交易的合约(比如ag2604):" << std::endl;
    interactiveOrder();

    std::cout << "\n>>> 请检查上述操作是否被系统拦截或返回了正确的错误码。\n" << std::endl;
}


// [2.5.1] 暂停交易
void testPauseAndCancel() {
    std::cout << "\n=== [2.5.1] 暂停交易 ===" << std::endl;

    std::cout << "\n[模拟暂停控制]" << std::endl;
    std::cout << "1. 限制交易权限？(y/n): ";
    char c1; std::cin >> c1; 
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // 清理缓冲区

    if (c1 == 'y' || c1 == 'Y') std::cout << "-> 模拟：权限已限制\n";

    std::cout << "2. 暂停策略？(y/n): ";
    char c2; std::cin >> c2; 
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // 清理缓冲区
    if (c2 == 'y' || c2 == 'Y') std::cout << "-> 模拟：策略已暂停\n";
}

// [2.6.1] 日志记录
void testLog(){
    std::cout << "\n=== [2.6.1] 日志记录 ===" << std::endl;
    std::cout << "\n请打开./build/logs/trading_system.log日志查看日志记录" << std::endl;
}


void showMenu() {
    std::cout << "\n========================================\n";
    std::cout << "       CTP 程序化报备菜单       \n";
    std::cout << "========================================\n";
    std::cout << "  1. [2.1.2] 开仓/平仓测试\n";
    std::cout << "  2. [2.1.2] 撤单测试\n";
    std::cout << "  3. [2.2.1] 系统连接异常监测功能\n";
    std::cout << "  4. [2.2.2] 报撤单笔数监测功能\n";
    std::cout << "  5. [2.3.1] 阈值设置及预警功能\n";
    std::cout << "  6. [2.4.1] 订单合约代码错误测试\n";
    std::cout << "  7. [2.4.1] 订单价格最小变动价位错误测试\n";
    std::cout << "  8. [2.4.1] 委托数量超单笔最大委托数量测试\n";
    std::cout << "  9. [2.4.1] 资金不足测试\n";
    std::cout << "  10. [2.4.1] 持仓不足测试\n";
    std::cout << "  11. [2.4.1] 市场状态错误测试\n";
    std::cout << "  12. [2.5.1] 暂停交易测试\n";
    std::cout << "  13. [2.6.1] 日志记录\n";
    std::cout << "  0. 退出程序\n";
    std::cout << "========================================\n";
    std::cout << "请选择测试项目 (0-13): ";
}

int main() {
    // 初始化环境
    fs::create_directories("./data");
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std:cout << "2.1.1 测试点 1：验证登录测试账号通过宏源期货交易信息系统（柜台）认证、完成账号登录" << std::endl;
    tpi.connect();
    tpi.wait_for_state(ConnectionState::CONNECTED);
    tpi.Authenticate();
    tpi.wait_for_state(ConnectionState::SETTLEMENT_CONFIRMED);

    // 初始化全部合约
    tpi.getAllInstrument();
    if (tpi.waitForInstrumentQuery()) {
        std::cout << "[系统] 合约数据初始化完成，共加载 " << tpi.getInstrumentVector().size() << " 个合约。" << std::endl;
        // 打印前几个合约
        const int PRINT_COUNT = 10; // 定义要打印的合约数量
        auto instruments = tpi.getInstrumentVector(); // 获取合约列表（已加锁）
        int actual_print = std::min(PRINT_COUNT, (int)instruments.size());

        std::cout << "\n---------- 前 " << actual_print << " 个合约信息 ----------" << std::endl;
        for (int i = 0; i < actual_print; ++i) {
            const auto& inst = instruments[i];
            // 打印核心字段：合约代码、交易所代码、合约名称、合约乘数
            std::cout << "序号: " << i + 1
                << " | 合约代码: " << inst.InstrumentID
                << " | 交易所代码: " << inst.ExchangeID
                << " | 合约名称: " << convertCTPString(inst.InstrumentName)
                << " | 合约乘数: " << inst.VolumeMultiple
                << " | 市价单最大下单量:" << inst.MaxMarketOrderVolume 
                << " | 最小变动价位" << inst.PriceTick << std::endl;

        }
        std::cout << "--------------------------------------------------\n" << std::endl;
    }
    else {
        std::cerr << "[严重错误] 合约数据初始化失败或超时！" << std::endl;
        return 0;
    }

    int choice = -1;
    while (g_running) {
        showMenu();

        if (!(std::cin >> choice)) {
            // 处理非数字输入
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "无效输入，请输入数字。\n";
            continue;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // 清除换行符

        switch (choice) {
        case 1:
            testInsertOrder();
            break;
        case 2:
            testDeleteOrder();
            break;
        case 3:
            testNetworkStability();
            break;
        case 4:
            testStatistics();
            break;
        case 5:
            testThresholds();
            break;
        case 6:
            testCodeError();
            break;
        case 7:
            testPriceError();
            break;
        case 8:
            testVolumeError();
            break;
        case 9: 
            testInsufficientFunds();
            break;
        case 10:
            testInsufficientPosition();
            break;
        case 11:
            testTimeError();
            break;
        case 12:
            testPauseAndCancel();
            break;
        case 13:
            testLog();
            break;
        case 0:
            std::cout << "正在退出程序...\n";
            g_running = false;
            break;
        default:
            std::cout << "无效的选择，请重新输入。\n";
            break;
        }

        if (!g_running) break;

         // 可选：每次测试完暂停一下，让用户看清结果
         std::cout << "\n按回车键返回菜单...";
         std::cin.get(); 
    }

    std::cout << "程序已安全退出。\n";
    return 0;
}