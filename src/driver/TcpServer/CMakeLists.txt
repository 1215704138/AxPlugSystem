cmake_minimum_required(VERSION 3.16)
project(TcpServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 基础TCP服务器插件 (无第三方依赖)
add_library(TcpServerPlugin SHARED
    include/TcpServer.h
    src/TcpServer.cpp
    src/module.cpp
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
)
target_link_libraries(TcpServerPlugin PRIVATE AxInterface ws2_32)
target_compile_definitions(TcpServerPlugin PRIVATE WIN32_LEAN_AND_MEAN)

# Boost高性能实现 - 仅在Boost可用时构建
set(BOOST_ROOT "${CMAKE_SOURCE_DIR}/deps/boost")
set(BOOST_INCLUDE_DIR "${BOOST_ROOT}/include")
set(BOOST_LIB_DIR "${BOOST_ROOT}/lib")

if(EXISTS "${BOOST_INCLUDE_DIR}/boost")
    message(STATUS "[TcpServer] Found Boost headers, building BoostTcpServerPlugin")
    add_library(BoostTcpServerPlugin SHARED
        include/BoostTcpServer.h
        src/BoostTcpServer.cpp
        src/boost_module.cpp
        ${CMAKE_SOURCE_DIR}/src/driver/TcpClient/src/BoostTcpClient.cpp
    )
    setup_plugin_target(BoostTcpServerPlugin)
    set_target_properties(BoostTcpServerPlugin PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        SKIP_BUILD_RPATH TRUE
    )
    target_include_directories(BoostTcpServerPlugin PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/driver/TcpServer
        ${CMAKE_SOURCE_DIR}/src/driver/TcpClient
        ${BOOST_INCLUDE_DIR}
    )
    target_link_libraries(BoostTcpServerPlugin PRIVATE AxInterface ws2_32 BoostTcpClientPlugin)
    target_compile_definitions(BoostTcpServerPlugin PRIVATE WIN32_LEAN_AND_MEAN BOOST_ALL_NO_LIB _WIN32_WINNT=0x0601)
else()
    message(STATUS "[TcpServer] Boost not found, skipping BoostTcpServerPlugin")
endif()

install(TARGETS TcpServerPlugin
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)

if(TARGET BoostTcpServerPlugin)
    install(TARGETS BoostTcpServerPlugin
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
        ARCHIVE DESTINATION lib
    )
endif()

