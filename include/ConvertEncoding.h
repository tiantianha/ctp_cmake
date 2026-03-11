#include <string>
#include <cstring>
#include <iostream>
#include <vector>
#include <cerrno> // 补充errno头文件

// 优化编码转换函数：支持GB18030、添加错误忽略、增强兼容性
// std::string convertEncoding(const std::string& input, const char* from, const char* to);

// 适配CTP的编码转换：优先尝试GB18030（兼容GBK）
std::string convertCTPString(const char* ctpStr);


