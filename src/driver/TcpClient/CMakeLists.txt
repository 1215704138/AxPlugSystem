# TcpClient CMakeLists.txt - v3 (Multi-Export)

# 基础TCP客户端插件 (集成 Boost 实现)
set(BOOST_ROOT "${CMAKE_SOURCE_DIR}/deps/boost")
set(BOOST_INCLUDE_DIR "${BOOST_ROOT}/include")
set(BOOST_LIB_DIR "${BOOST_ROOT}/lib")

add_library(TcpClientPlugin SHARED
    include/TcpClient.h
    include/BoostTcpClient.h
    src/TcpClient.cpp
    src/BoostTcpClient.cpp
    src/module.cpp
)

setup_plugin_target(TcpClientPlugin)
set_target_properties(TcpClientPlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    SKIP_BUILD_RPATH TRUE
)

target_include_directories(TcpClientPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/driver/TcpClient
    ${BOOST_INCLUDE_DIR}
)

# 链接依赖 (Boost 必须存在)
if(EXISTS "${BOOST_LIB_DIR}/libboost_system.lib")
    target_link_libraries(TcpClientPlugin PRIVATE 
        AxInterface 
        ws2_32
        "${BOOST_LIB_DIR}/libboost_system.lib"
        "${BOOST_LIB_DIR}/libboost_thread.lib"
        "${BOOST_LIB_DIR}/libboost_date_time.lib"
    )
    target_compile_definitions(TcpClientPlugin PRIVATE 
        WIN32_LEAN_AND_MEAN
        BOOST_ASIO_ENABLE_HANDLER_TRACKING
        BOOST_ALL_NO_LIB
        HAS_BOOST_LIBS
        _WIN32_WINNT=0x0601
    )
else()
    # Fallback
    target_link_libraries(TcpClientPlugin PRIVATE 
        AxInterface 
        ws2_32
    )
    target_compile_definitions(TcpClientPlugin PRIVATE 
        WIN32_LEAN_AND_MEAN
        BOOST_ALL_NO_LIB
        _WIN32_WINNT=0x0601
    )
endif()

install(TARGETS TcpClientPlugin
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)
