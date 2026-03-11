cmake_minimum_required(VERSION 3.10)
project("ctp_cmake")

# 设置 C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 线程支持
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# 平台相关 API 目录
if(WIN32)
    set(CTP_API_DIR "${CMAKE_CURRENT_SOURCE_DIR}/20250617_traderapi64_se_windows")
else()
    set(CTP_API_DIR "${CMAKE_CURRENT_SOURCE_DIR}/v6.7.11_20250617_api_traderapi_se_linux64")
endif()


# === 列出所有 .cpp 源文件 ===
add_executable(ctp_cmake
   src/main.cpp
   src/CustTpi.cpp
   src/CustMDSpi.cpp
   src/Logger.cpp
   src/ConvertEncoding.cpp

   # 列出头文件
   include/CtpEvent.h
   include/ConcurrentQueue.h
   include/CustMDSpi.h
   include/CustTpi.h
   include/main.h
   include/Logger.h
   include/LoadConfig.h
   include/ConvertEncoding.h
)

# 头文件目录（包含 CTP 和当前目录）
target_include_directories(ctp_cmake PRIVATE 
    ${CTP_API_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include  
    ${CMAKE_CURRENT_SOURCE_DIR}  
)

# 定义项目根目录宏 
# 这将允许 C++ 代码通过 #ifdef PROJECT_ROOT_DIR 获取绝对路径
# 使得配置文件可以始终放在 CMakeLists.txt 同级目录，而无需复制到 build 目录
target_compile_definitions(ctp_cmake PRIVATE 
    PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}"
)

if(WIN32)
    # Windows: 链接 .lib
    target_link_libraries(ctp_cmake
        "${CTP_API_DIR}/thostmduserapi_se.lib"
        "${CTP_API_DIR}/thosttraderapi_se.lib"
        Threads::Threads
    )

    if(MSVC)
        set_property(TARGET ctp_cmake PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    endif()

    # 复制 DLL
    file(GLOB CTP_DLL_FILES "${CTP_API_DIR}/*.dll")
    foreach(DLL_FILE ${CTP_DLL_FILES})
        add_custom_command(TARGET ctp_cmake POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DLL_FILE}"
                $<TARGET_FILE_DIR:ctp_cmake>
        )
    endforeach()

else()
    # =============== Linux (标准方式) ===============
    # 指定库搜索路径
    target_link_directories(ctp_cmake PRIVATE ${CTP_API_DIR})

    # 使用 -l 链接（库名去掉 "lib" 和 ".so"）
    target_link_libraries(ctp_cmake
        thostmduserapi_se      # 对应 libthostmduserapi_se.so
        thosttraderapi_se      # 对应 libthosttraderapi_se.so
        Threads::Threads
        dl
    )

    # 复制 .so 到输出目录（方便运行）
    add_custom_command(TARGET ctp_cmake POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CTP_API_DIR}/libthostmduserapi_se.so"
            $<TARGET_FILE_DIR:ctp_cmake>
    )
    add_custom_command(TARGET ctp_cmake POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CTP_API_DIR}/libthosttraderapi_se.so"
            $<TARGET_FILE_DIR:ctp_cmake>
    )
endif()
