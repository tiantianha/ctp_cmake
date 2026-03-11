# CTP 程序化交易接口封装 (CTP CMake Wrapper)

本项目是基于 **上期技术 (SHFE) CTP API** (版本 v6.7.11) 的 C++ 封装实现。旨在通过 **CMake** 构建系统提供跨平台（Windows/Linux）的编译支持，简化量化交易策略的开发与部署流程。
## 主要用于 **国内期货CTP程序化报备**

## 🚀 项目特点

- **原生 CTP 支持**: 基于最新的 `v6.7.11_20250617`  TraderAPI 和 MdApi。
- **CMake 构建**: 摒弃传统的 `.sln` 或 `.mak`，使用现代化的 CMake 管理依赖和构建目标，支持 VS Code、Visual Studio 和 Linux GCC/Clang。
- **跨平台兼容**: 
  - ✅ Windows (x64): 包含完整的 DLL 和 Lib 支持。
  - ✅ Linux (x64): 包含对应的 SO 库支持。
- **模块化设计**: 
  - 封装了行情 (`MdApi`) 和交易 (`TraderApi`) 的核心回调。
  - 内置日志模块 (`Logger`) 和配置加载模块 (`LoadConfig`)。
- **编码转换**: 内置 `ConvertEncoding` 模块，自动处理 GBK (CTP 默认) 与 UTF-8 之间的转换，解决中文乱码问题。

## 📂 目录结构

```text
ctp_cmake/
├── CMakeLists.txt              # CMake 构建配置文件
├── CMakePresets.json           # VS Code / VS2019+ 预设配置
├── ctp_config.ini              # 示例配置文件 (账号、密码、服务器地址)
├── include/                    # 头文件
│   ├── ConvertEncoding.h       # 转换编码 
│   ├── main.h					# 主函数
│   ├── LoadConfig.h			# 读取账号等配置信息
│   ├── Logger.h                # 日志接口
│   ├── CustMDSpi.h             # 行情回调实现
│   └── CustTpi.h               # 交易回调实现
├── src/                        # 源文件
│   ├── main.cpp                # 程序入口
│   ├── ConvertEncoding.cpp     # 转换编码 
│   ├── CustMDSpi.cpp           # 行情逻辑
│   ├── CustTpi.cpp             # 交易逻辑
│   └── Logger.cpp              # 日志实现
├── 20250617_traderapi64_se_windows/  # Windows 平台 CTP SDK (dll/lib/h)
└── v6.7.11_20250617_api_traderapi_se_linux64/ # Linux 平台 CTP SDK (so/h)
```


## 🛠️ 环境要求

- **编译器**:
  - **Windows**: MSVC (Visual Studio 2019+) 或 MinGW-w64
  - **Linux**: GCC 7+ 或 Clang
- **构建工具**: CMake 3.15+
- **依赖**: 无第三方外部依赖（CTP SDK 已包含在项目内）

## ⚙️ 编译指南

## Windows (Visual Studio / VS Code)


#### 使用 CMake Presets (推荐)
如果你使用 VS Code 或 Visual Studio 2019+，可以直接使用预设配置：

```bash
# 配置项目
cmake --preset=default

# 构建项目
cmake --build --preset=default
```

## 🐧 Linux 编译与运行指南

本项目支持主流的 Linux 发行版（Ubuntu, CentOS, Debian 等）。请按照以下步骤进行构建和运行。

### 1. 安装依赖

在开始之前，请确保您的系统已安装必要的构建工具。

**Ubuntu / Debian:**
```bash
sudo apt-get update
sudo apt-get install -y cmake g++ make
```

### 2. 构建项目

```bash
# 创建并进入构建目录
mkdir -p build && cd build

# 配置 CMake
cmake .. 

# 开始编译 (使用所有可用 CPU 核心加速)
make 

# 运行，需要使用sudo命令，方便期货公司柜台读取你的电脑主板等信息
sudo ./ctp_cmake
```

## 🚀 快速开始

- 示例配置 (ctp_config.ini):
```bash
[Account]
# 期货公司代码 (SimNow 通常为 9999)
BrokerID=9999
# 用户账号
UserID=123456
# 登录密码
Password=123456
# 行情服务器地址 (SimNow 第一组)
MdAddr=tcp://182.254.243.31:30011
# 交易服务器地址 (SimNow 第一组)
TraderAddr=tcp://182.254.243.31:30001
```
##  📄 许可证与免责声明

- **版权归属: CTP API 版权归 上海期货信息技术有限公司 (SHFE) 所有**:
- **使用限制: 本项目仅供学习、研究和仿真测试使用**:
- **风险提示: 使用者需遵守上期技术的相关软件授权协议。本代码不提供实盘交易保证，实盘风险自负**:

## 🤝 贡献
- **欢迎提交 Issue 和 Pull Request 来改进这个封装库！**:
- **Last Updated: 2026-03-10**:


## 🤝 联系我
![联系作者](./pic/wechat.jpg)
