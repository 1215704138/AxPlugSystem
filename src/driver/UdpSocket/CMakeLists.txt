# UdpSocket CMakeLists.txt - v2

# 基础UDP插件 (无第三方依赖)
add_library(UdpSocketPlugin SHARED
    include/UdpSocket.h
    src/UdpSocket.cpp
    src/module.cpp
)
setup_plugin_target(UdpSocketPlugin)
set_target_properties(UdpSocketPlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
target_include_directories(UdpSocketPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/driver/UdpSocket
)
target_link_libraries(UdpSocketPlugin PRIVATE AxInterface ws2_32)
target_compile_definitions(UdpSocketPlugin PRIVATE WIN32_LEAN_AND_MEAN)

# Boost高性能实现 - 仅在Boost可用时构建
set(BOOST_ROOT "${CMAKE_SOURCE_DIR}/deps/boost")
set(BOOST_INCLUDE_DIR "${BOOST_ROOT}/include")

if(EXISTS "${BOOST_INCLUDE_DIR}/boost")
    message(STATUS "[UdpSocket] Found Boost headers, building BoostUdpSocketPlugin")
    add_library(BoostUdpSocketPlugin SHARED
        include/BoostUdpSocket.h
        src/BoostUdpSocket.cpp
        src/boost_module.cpp
    )
    setup_plugin_target(BoostUdpSocketPlugin)
    set_target_properties(BoostUdpSocketPlugin PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )
    target_include_directories(BoostUdpSocketPlugin PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/driver/UdpSocket
        ${BOOST_INCLUDE_DIR}
    )
    target_link_libraries(BoostUdpSocketPlugin PRIVATE AxInterface ws2_32)
    target_compile_definitions(BoostUdpSocketPlugin PRIVATE WIN32_LEAN_AND_MEAN BOOST_ALL_NO_LIB _WIN32_WINNT=0x0601)
else()
    message(STATUS "[UdpSocket] Boost not found, skipping BoostUdpSocketPlugin")
endif()

install(TARGETS UdpSocketPlugin
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)

if(TARGET BoostUdpSocketPlugin)
    install(TARGETS BoostUdpSocketPlugin
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
        ARCHIVE DESTINATION lib
    )
endif()

