# TcpClient CMakeLists.txt - v2

add_library(TcpClientPlugin SHARED
    include/TcpClient.h
    src/TcpClient.cpp
    src/module.cpp
)

# Boost高性能实现 - 仅在Boost可用时构建
set(BOOST_ROOT "${CMAKE_SOURCE_DIR}/deps/boost")
set(BOOST_INCLUDE_DIR "${BOOST_ROOT}/include")
set(BOOST_LIB_DIR "${BOOST_ROOT}/lib")

if(EXISTS "${BOOST_INCLUDE_DIR}/boost")
    message(STATUS "Found Boost headers at ${BOOST_INCLUDE_DIR}")
    
    add_library(BoostTcpClientPlugin SHARED
        include/BoostTcpClient.h
        src/BoostTcpClient.cpp
        src/boost_module.cpp
    )
    
    # 插件导出宏
    setup_plugin_target(TcpClientPlugin)
    setup_plugin_target(BoostTcpClientPlugin)
    
    # 输出目录
    set_target_properties(TcpClientPlugin PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )
    
    set_target_properties(BoostTcpClientPlugin PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )
    
    target_include_directories(TcpClientPlugin PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/driver/TcpClient
    )
    
    target_include_directories(BoostTcpClientPlugin PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/driver/TcpClient
        ${BOOST_INCLUDE_DIR}
    )
    
    # 插件只需要头文件和 ws2_32
    target_link_libraries(TcpClientPlugin PRIVATE AxInterface ws2_32)
    target_compile_definitions(TcpClientPlugin PRIVATE WIN32_LEAN_AND_MEAN)
    
    # 检查Boost库是否存在
    if(EXISTS "${BOOST_LIB_DIR}/libboost_system.lib")
        message(STATUS "Found Boost libraries at ${BOOST_LIB_DIR}")
        target_link_libraries(BoostTcpClientPlugin PRIVATE 
            AxInterface 
            ws2_32
            "${BOOST_LIB_DIR}/libboost_system.lib"
            "${BOOST_LIB_DIR}/libboost_thread.lib"
            "${BOOST_LIB_DIR}/libboost_date_time.lib"
        )
        target_compile_definitions(BoostTcpClientPlugin PRIVATE 
            WIN32_LEAN_AND_MEAN
            BOOST_ASIO_ENABLE_HANDLER_TRACKING
            BOOST_ALL_NO_LIB
            HAS_BOOST_LIBS
            _WIN32_WINNT=0x0601
        )
    else()
        message(WARNING "Boost libraries not found at ${BOOST_LIB_DIR}, BoostTcpClientPlugin will be header-only")
        target_link_libraries(BoostTcpClientPlugin PRIVATE 
            AxInterface 
            ws2_32
        )
        target_compile_definitions(BoostTcpClientPlugin PRIVATE 
            WIN32_LEAN_AND_MEAN
            BOOST_ALL_NO_LIB
            _WIN32_WINNT=0x0601
        )
    endif()
else()
    message(WARNING "Boost headers not found at ${BOOST_INCLUDE_DIR}, BoostTcpClientPlugin will not be built")
    
    # 插件导出宏
    setup_plugin_target(TcpClientPlugin)
    
    # 输出目录
    set_target_properties(TcpClientPlugin PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )
    
    target_include_directories(TcpClientPlugin PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/driver/TcpClient
    )
    
    # 插件只需要头文件和 ws2_32
    target_link_libraries(TcpClientPlugin PRIVATE AxInterface ws2_32)
    target_compile_definitions(TcpClientPlugin PRIVATE WIN32_LEAN_AND_MEAN)
endif()

install(TARGETS TcpClientPlugin
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)

if(TARGET BoostTcpClientPlugin)
    install(TARGETS BoostTcpClientPlugin
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
        ARCHIVE DESTINATION lib
    )
endif()

