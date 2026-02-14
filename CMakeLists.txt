cmake_minimum_required(VERSION 3.15)



# 项目名称和版本

project(AxPlug VERSION 1.0.0 LANGUAGES CXX)



# 设置 CMake 策略来抑制警告

if(POLICY CMP0074)

    cmake_policy(SET CMP0074 NEW)

endif()



# 强制使用 C++17

set(CMAKE_CXX_STANDARD 17)


set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 生成 compile_commands.json 用于 IDE 智能提示
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)




# 设置构建类型为 Debug (用于调试)

if(NOT CMAKE_BUILD_TYPE)

    set(CMAKE_BUILD_TYPE Debug)

endif()

# 🔧 设置标准运行时库 (确保跨DLL调用兼容性)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    add_compile_definitions(NOMINMAX)
endif()

# 抑制 MSVC STL 扩展警告

add_compile_definitions(

    _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING

    _SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS

)

# 为所有插件项目设置导出宏
function(setup_plugin_target target_name)
    target_compile_definitions(${target_name} PRIVATE AX_PLUGIN_EXPORTS)
endfunction()



# 修复编码问题

if(MSVC)

    add_compile_options(/utf-8)

endif()



# 设置全局输出目录 - 统一到 bin/Debug

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Debug")

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Debug")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Debug")



# 为多配置生成器设置输出目录

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug")

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug")



set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release")

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release")



# --- 包含 FetchContent 模块 ---

include(FetchContent)

# --- 输出目录管理 ---

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # .lib 文件

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # .dll 文件

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # .exe 文件



# --- 定义公共接口库 (Header-only) ---

add_library(AxInterface INTERFACE)

target_include_directories(AxInterface INTERFACE 

    ${CMAKE_SOURCE_DIR}/include

    ${CMAKE_SOURCE_DIR}/include/core

    ${CMAKE_SOURCE_DIR}/include/driver

    ${CMAKE_SOURCE_DIR}/include/business

)



# --- 添加子模块 ---

add_subdirectory(src/AxCore)

add_subdirectory(src/core/LoggerService)

add_subdirectory(src/business/MathPlugin)

add_subdirectory(src/driver/TcpClient)

add_subdirectory(src/driver/TcpServer)

add_subdirectory(src/driver/UdpSocket)

add_subdirectory(src/core/ImageUnifyService)





# --- 打印配置信息 ---

message(STATUS "=== AxPlug Configuration ===")

message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")

message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")

message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")



message(STATUS "Output Directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

message(STATUS "=============================")


# --- 安装/发布配置 ---

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/publish" CACHE PATH "Default install prefix" FORCE)
endif()

# 安装头文件
install(DIRECTORY include/ DESTINATION include)

# --- 添加测试子目录 ---
option(AXPLUG_BUILD_TESTS "Build AxPlug tests" ON)

if(AXPLUG_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test")
    add_subdirectory(test)
endif()


