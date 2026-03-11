// Logger.h
#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>

class Logger {
public:
    enum Level {
        INFO,
        WARN,
        ERROR,
        TRADE  // 专用于交易记录
    };

    static Logger& instance(); // 单例

    void log(Level level, const std::string& message);
    
    // 快捷方法
    void info(const std::string& msg)  { log(INFO, msg); }
    void warn(const std::string& msg)  { log(WARN, msg); }
    void error(const std::string& msg) { log(ERROR, msg); }
    void trade(const std::string& msg) { log(TRADE, msg); }

private:
    Logger();
    ~Logger();

    std::ofstream m_file;
    std::mutex m_mutex;

    std::string getCurrentTime();
    std::string levelToString(Level level);
};
