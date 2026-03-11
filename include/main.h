// ctp_cmake.h: 标准系统包含文件的包含文件
// 或项目特定的包含文件。

#pragma once

#include <iostream>

// TODO: 在此处引用程序需要的其他标头。
void signal_handler(int sig);
void wait_for_ready();
std::string generateOrderRef();
void interactiveOrder();
void testInsertOrder();
void testDeleteOrder();
void testNetworkStability();
void testStatistics();
void testThresholds();
void testCodeError();
void testPriceError();
void testVolumeError();
void testInsufficientFunds();
void testInsufficientPosition();
void testTimeError();
void testPauseAndCancel();
void testLog();
void showMenu();


