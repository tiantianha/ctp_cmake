// #include <iconv.h>
//#include <string>
//#include <cstring>
//#include <iostream>
//#include <vector>
//#include <cerrno> // 补充errno头文件
//
//// 优化编码转换函数：支持GB18030、添加错误忽略、增强兼容性
//std::string convertEncoding(const std::string& input, const char* from, const char* to) {
//    //if (input.empty()) return "";
//    //// 关键修改1：添加//IGNORE忽略无效字符，//TRANSLIT替换无法转换的字符
//    //std::string from_encoding = std::string(from) + "//IGNORE//TRANSLIT";
//    //std::string to_encoding = std::string(to) + "//IGNORE//TRANSLIT";
//    //iconv_t cd = iconv_open(to_encoding.c_str(), from_encoding.c_str());
//    //if (cd == (iconv_t)-1) {
//    //    std::cerr << "[Error] iconv_open failed: " << strerror(errno)
//    //        << " (from: " << from << ", to: " << to << ")" << std::endl;
//    //    return input;
//    //}
//    //// 保留原有缓冲逻辑，仅优化编码参数
//    //size_t inbytes_left = input.size();
//    //std::vector<char> inbuf_vec(input.begin(), input.end());
//    //char* inbuf = inbuf_vec.data();
//
//    //size_t outbytes_total = inbytes_left * 4;
//    //size_t outbytes_left = outbytes_total;
//    //std::vector<char> outbuf_vec(outbytes_total, 0);
//    //char* outptr = outbuf_vec.data();
//
//    //// 执行转换（iconv会修改指针和剩余字节数）
//    //size_t ret = iconv(cd, &inbuf, &inbytes_left, &outptr, &outbytes_left);
//    //if (ret == (size_t)-1) {
//    //    std::cerr << "[Warning] iconv conversion warning: " << strerror(errno)
//    //        << " (input: " << input.substr(0, 20) << "...)" << std::endl;
//    //    // 转换失败仍尝试返回已转换部分，而非直接返回原串
//    //    size_t written_size = outbytes_total - outbytes_left;
//    //    iconv_close(cd);
//    //    return written_size > 0 ? std::string(outbuf_vec.data(), written_size) : input;
//    //}
//
//    //iconv_close(cd);
//    //size_t written_size = outbytes_total - outbytes_left;
//    //return std::string(outbuf_vec.data(), written_size);
//    return "";
//}
//
//// 适配CTP的编码转换：优先尝试GB18030（兼容GBK）
//std::string convertCTPString(const char* ctpStr) {
//    if (!ctpStr) return "";
//    std::string str(ctpStr);
//    if (str.empty()) return "";
//
//    // 关键修改2：CTP接口优先用GB18030（覆盖GBK场景）
//    std::string utf8_str = convertEncoding(str, "GB18030", "UTF-8");
//    // 兜底：如果转换后还是乱码（长度异常），尝试GBK
//    if (utf8_str.empty() || utf8_str.size() < str.size() / 2) {
//        utf8_str = convertEncoding(str, "GBK", "UTF-8");
//    }
//    return utf8_str;
//}


#include <string>
#include <cstring>
#include <iostream>
#include <vector>
#include <cerrno>

#ifdef _WIN32
    // Windows平台：使用Windows API
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #undef WIN32_LEAN_AND_MEAN
#else
    // Linux平台：使用iconv
    #include <iconv.h>
#endif

// 优化编码转换函数：支持GB18030、添加错误忽略、增强兼容性
std::string convertEncoding(const std::string& input, const char* from, const char* to) {
    if (input.empty()) return "";

#ifdef _WIN32
    // Windows实现：使用Windows API
    UINT from_cp = CP_UTF8;
    UINT to_cp = CP_UTF8;

    // 编码名称映射到Windows代码页
    if (strcmp(from, "UTF-8") == 0 || strcmp(from, "utf8") == 0) from_cp = CP_UTF8;
    else if (strcmp(from, "GB18030") == 0) from_cp = 54936;  // GB18030代码页
    else if (strcmp(from, "GBK") == 0) from_cp = 936;        // GBK代码页
    else if (strcmp(from, "GB2312") == 0) from_cp = 936;     // GB2312使用GBK代码页

    if (strcmp(to, "UTF-8") == 0 || strcmp(to, "utf8") == 0) to_cp = CP_UTF8;
    else if (strcmp(to, "GB18030") == 0) to_cp = 54936;
    else if (strcmp(to, "GBK") == 0) to_cp = 936;
    else if (strcmp(to, "GB2312") == 0) to_cp = 936;

    // 第一步：从源编码转换到UTF-16
    int wide_len = MultiByteToWideChar(from_cp, 0, input.c_str(), -1, NULL, 0);
    if (wide_len <= 0) {
        std::cerr << "[Error] MultiByteToWideChar failed, error: " << GetLastError()
            << " (from: " << from << ")" << std::endl;
        return input;
    }

    std::vector<wchar_t> wide_buf(wide_len);
    if (MultiByteToWideChar(from_cp, 0, input.c_str(), -1, wide_buf.data(), wide_len) == 0) {
        std::cerr << "[Error] MultiByteToWideChar conversion failed, error: " << GetLastError() << std::endl;
        return input;
    }

    // 第二步：从UTF-16转换到目标编码
    int target_len = WideCharToMultiByte(to_cp, 0, wide_buf.data(), -1, NULL, 0, NULL, NULL);
    if (target_len <= 0) {
        std::cerr << "[Error] WideCharToMultiByte failed, error: " << GetLastError()
            << " (to: " << to << ")" << std::endl;
        return input;
    }

    std::vector<char> target_buf(target_len);
    if (WideCharToMultiByte(to_cp, 0, wide_buf.data(), -1, target_buf.data(), target_len, NULL, NULL) == 0) {
        std::cerr << "[Error] WideCharToMultiByte conversion failed, error: " << GetLastError() << std::endl;
        return input;
    }

    // 返回结果（去掉字符串末尾的\0）
    return std::string(target_buf.data(), target_len - 1);

#else
    // Linux实现：使用iconv
    // 添加//IGNORE忽略无效字符，//TRANSLIT替换无法转换的字符
    std::string from_encoding = std::string(from) + "//IGNORE//TRANSLIT";
    std::string to_encoding = std::string(to) + "//IGNORE//TRANSLIT";

    iconv_t cd = iconv_open(to_encoding.c_str(), from_encoding.c_str());
    if (cd == (iconv_t)-1) {
        std::cerr << "[Error] iconv_open failed: " << strerror(errno)
            << " (from: " << from << ", to: " << to << ")" << std::endl;
        return input;
    }

    size_t inbytes_left = input.size();
    std::vector<char> inbuf_vec(input.begin(), input.end());
    char* inbuf = inbuf_vec.data();

    size_t outbytes_total = inbytes_left * 4;
    size_t outbytes_left = outbytes_total;
    std::vector<char> outbuf_vec(outbytes_total, 0);
    char* outptr = outbuf_vec.data();

    size_t ret = iconv(cd, &inbuf, &inbytes_left, &outptr, &outbytes_left);
    if (ret == (size_t)-1) {
        std::cerr << "[Warning] iconv conversion warning: " << strerror(errno)
            << " (input: " << input.substr(0, 20) << "...)" << std::endl;
        size_t written_size = outbytes_total - outbytes_left;
        iconv_close(cd);
        return written_size > 0 ? std::string(outbuf_vec.data(), written_size) : input;
    }

    iconv_close(cd);
    size_t written_size = outbytes_total - outbytes_left;
    return std::string(outbuf_vec.data(), written_size);
#endif
}

// 简单的UTF-8检测函数
bool isLikelyUTF8(const std::string& str) {
    size_t i = 0;
    while (i < str.size()) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if (c < 0x80) {
            i += 1;
        }
        else if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 >= str.size() || (static_cast<unsigned char>(str[i + 1]) & 0xC0) != 0x80)
                return false;
            i += 2;
        }
        else if (c >= 0xE0 && c <= 0xEF) {
            if (i + 2 >= str.size() ||
                (static_cast<unsigned char>(str[i + 1]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(str[i + 2]) & 0xC0) != 0x80)
                return false;
            i += 3;
        }
        else if (c >= 0xF0 && c <= 0xF4) {
            if (i + 3 >= str.size() ||
                (static_cast<unsigned char>(str[i + 1]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(str[i + 2]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(str[i + 3]) & 0xC0) != 0x80)
                return false;
            i += 4;
        }
        else {
            return false;
        }
    }
    return true;
}

// 适配CTP的编码转换：优先尝试GB18030（兼容GBK）
std::string convertCTPString(const char* ctpStr) {
    if (!ctpStr) return "";
    std::string str(ctpStr);
    if (str.empty()) return "";

    // 如果已经是UTF-8，直接返回
    if (isLikelyUTF8(str)) {
        return str;
    }

    // 优先用GB18030（覆盖GBK场景）
    std::string utf8_str = convertEncoding(str, "GB18030", "UTF-8");

    // 验证转换结果
    if (utf8_str.empty() || utf8_str.size() < str.size() / 3 || !isLikelyUTF8(utf8_str)) {
        // 兜底：尝试GBK
        utf8_str = convertEncoding(str, "GBK", "UTF-8");

        // 再次验证
        if (utf8_str.empty() || utf8_str.size() < str.size() / 3 || !isLikelyUTF8(utf8_str)) {
            // 最后尝试GB2312
            utf8_str = convertEncoding(str, "GB2312", "UTF-8");
        }
    }

    return utf8_str;
}

#ifdef _WIN32
    // Windows下获取系统默认ANSI编码（可选辅助函数）
    std::string getSystemAnsiEncoding() {
        UINT cp = GetACP();
        switch (cp) {
        case 936: return "GBK";
        case 54936: return "GB18030";
        case 65001: return "UTF-8";
        default: return "GBK";
        }
    }
#endif
