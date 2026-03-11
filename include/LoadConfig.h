#pragma once
#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <filesystem> // 确保包含
namespace fs = std::filesystem;

// 定义配置结构体，包含所有需要的字段
struct CtpConfig {
    std::string BrokerID;
    std::string UserID;
    std::string InvestorID;
    std::string Password;
    std::string AuthCode;
    std::string AppID;
    std::string ProductInfo;

    // 交易前置
    std::string TradeFront;
    // 行情前置
    std::string MdFront;

    std::string CurrencyID;

    // 默认构造函数（防止未加载时的空值错误）
    CtpConfig() {}

    // 验证配置是否完整
    bool isValid() const {
        return !BrokerID.empty() && !UserID.empty() && !Password.empty();
    }
};

class ConfigLoader {
private:
    inline static CtpConfig m_config;      // 添加 inline
    inline static bool m_loaded = false;    // 添加 inline
    inline static std::mutex m_loadMutex;   // 添加 inline

    // 单例模式，私有构造函数，强制使用静态方法，防止创建多个类
    ConfigLoader() {}

    // 内部解析逻辑
    static void parseFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "[ConfigLoader] Error: Cannot open config file: " << filename << std::endl;
            return;
        }

        std::string line, section;
        while (std::getline(file, line)) {
            // 去除首尾空白
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            // 识别段 [Section]
            if (line[0] == '[' && line.back() == ']') {
                section = line.substr(1, line.length() - 2);
                continue;
            }

            // 识别 Key=Value
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = trim(line.substr(0, pos));
                std::string value = trim(line.substr(pos + 1));

                // 统一映射，不区分段（或者可以根据 section 做更细的控制）
                if (key == "BrokerID") m_config.BrokerID = value;
                else if (key == "UserID") m_config.UserID = value;
                else if (key == "InvestorID") m_config.InvestorID = value;
                else if (key == "Password") m_config.Password = value;
                else if (key == "AuthCode") m_config.AuthCode = value;
                else if (key == "AppID") m_config.AppID = value;
                else if (key == "ProductInfo") m_config.ProductInfo = value;
                else if (key == "CurrencyID") m_config.CurrencyID = value;

                // 网络地址区分
                else if (key == "TradeFront") m_config.TradeFront = value;
                else if (key == "MdFront") m_config.MdFront = value;
                // 兼容旧配置：如果没有 MdFront，且 Front 存在，可能用于行情（视具体.ini而定）
                else if (key == "Front") {
                    // 简单策略：如果还没设 MdFront，先暂存，或者由调用者决定
                    // 这里建议用户在 ini 里明确写 TradeFront 和 MdFront
                    if (m_config.MdFront.empty()) m_config.MdFront = value;
                }
            }
        }
        file.close();
        m_loaded = true;

        if (!m_config.isValid()) {
            std::cerr << "[ConfigLoader] Warning: Configuration loaded but missing critical fields." << std::endl;
        }
        else {
            std::cout << "[ConfigLoader] Success: Loaded config for User " << m_config.UserID << std::endl;
        }
    }

    // 辅助函数：trim
    static std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (std::string::npos == first) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

public:
    // 全局访问点：获取配置实例
    // 如果尚未加载，则尝试加载默认文件
    static const CtpConfig& getInstance(const std::string& filename = "ctp_config.ini") {
        std::lock_guard<std::mutex> lock(m_loadMutex);
        if (!m_loaded) {
            std::string fullPath = filename;

            // 【强制修复逻辑】
            // 如果文件不存在于当前目录，且定义了 PROJECT_ROOT_DIR，则强制拼接
            if (!fs::exists(fullPath)) {
                #ifdef PROJECT_ROOT_DIR
                    // 拼接：根目录 + "/" + 文件名
                    fullPath = std::string(PROJECT_ROOT_DIR) + "/" + filename;
                    std::cout << "[ConfigLoader] File not found in CWD, trying absolute path: " << fullPath << std::endl;

                    // 再次检查是否存在
                    if (!fs::exists(fullPath)) {
                        std::cerr << "[ConfigLoader] Critical Error: Config file not found at: " << fullPath << std::endl;
                        throw std::runtime_error("Config file not found even with absolute path");
                    }
                #else
                    std::cerr << "[ConfigLoader] Critical Error: Config file not found in CWD and PROJECT_ROOT_DIR is not defined." << std::endl;
                    throw std::runtime_error("Config file not found");
                #endif
            }
            else {
                std::cout << "[ConfigLoader] Found config in current working directory: " << fullPath << std::endl;
            }

            parseFile(fullPath);
        }
        return m_config;
    }

    // 强制重新加载（用于热更新场景，一般不需要）
    static void reload(const std::string& filename = "ctp_config.ini") {
        std::lock_guard<std::mutex> lock(m_loadMutex);
        m_loaded = false;
        m_config = CtpConfig(); // 重置
        parseFile(filename);
    }
};

#endif // CONFIG_LOADER_H