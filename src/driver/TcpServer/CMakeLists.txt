cmake_minimum_required(VERSION 3.16)
project(TcpServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 基础TCP服务器插件 (集成 Boost 实现)
set(BOOST_ROOT "${CMAKE_SOURCE_DIR}/deps/boost")
set(BOOST_INCLUDE_DIR "${BOOST_ROOT}/include")
set(BOOST_LIB_DIR "${BOOST_ROOT}/lib")

add_library(TcpServerPlugin SHARED
    include/TcpServer.h
    include/BoostTcpServer.h
    src/TcpServer.cpp
    src/BoostTcpServer.cpp
    src/module.cpp
    ${CMAKE_SOURCE_DIR}/src/driver/TcpClient/src/BoostTcpClient.cpp
)

setup_plugin_target(TcpServerPlugin)
set_target_properties(TcpServerPlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    SKIP_BUILD_RPATH TRUE
)

target_include_directories(TcpServerPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/driver/TcpServer
    ${CMAKE_SOURCE_DIR}/src/driver/TcpClient
    ${BOOST_INCLUDE_DIR}
)

# 链接依赖 (Boost 必须存在)
if(EXISTS "${BOOST_LIB_DIR}/libboost_system.lib")
    target_link_libraries(TcpServerPlugin PRIVATE 
        AxInterface ws2_32
        "${BOOST_LIB_DIR}/libboost_system.lib"
        "${BOOST_LIB_DIR}/libboost_thread.lib"
        "${BOOST_LIB_DIR}/libboost_date_time.lib"
    )
    target_compile_definitions(TcpServerPlugin PRIVATE 
        WIN32_LEAN_AND_MEAN 
        BOOST_ASIO_ENABLE_HANDLER_TRACKING
        BOOST_ALL_NO_LIB 
        HAS_BOOST_LIBS
        _WIN32_WINNT=0x0601
    )
else()
     # Fallback if libs not found but headers exist (header-only boost)
     # But TcpServer uses Boost libs usually. 
     # Assuming header-only for Asio might work if configured, but let's stick to what worked for BoostTcpClient/Server
    target_link_libraries(TcpServerPlugin PRIVATE AxInterface ws2_32)
    target_compile_definitions(TcpServerPlugin PRIVATE WIN32_LEAN_AND_MEAN BOOST_ALL_NO_LIB _WIN32_WINNT=0x0601)
endif()

install(TARGETS TcpServerPlugin
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)

