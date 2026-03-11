// Logger.cpp
#include "Logger.h"
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

Logger::Logger() {
    // 确保 logs 目录存在
    fs::create_directories("logs");
    m_file.open("logs/trading_system.log", std::ios::app);
    if (!m_file.is_open()) {
        std::cerr << "❌ 无法打开日志文件！\n";
    }
}

Logger::~Logger() {
    if (m_file.is_open()) {
        m_file.close();
    }
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

std::string Logger::getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string Logger::levelToString(Level level) {
    switch (level) {
        case INFO:  return "INFO ";
        case WARN:  return "WARN ";
        case ERROR: return "ERROR";
        case TRADE: return "TRADE";
        default:    return "UNKNOWN";
    }
}

void Logger::log(Level level, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_file.is_open()) return;

    std::string line = "[" + getCurrentTime() + "] [" + levelToString(level) + "] " + message + "\n";
    m_file << line;
    m_file.flush(); // 确保立即写入

    // 可选：同时输出到控制台（便于调试）
    std::cout << line;
}
