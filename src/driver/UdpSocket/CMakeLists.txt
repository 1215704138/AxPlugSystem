# UdpSocket CMakeLists.txt - v2

# 基础UDP插件 (集成 Boost 实现)
set(BOOST_ROOT "${CMAKE_SOURCE_DIR}/deps/boost")
set(BOOST_INCLUDE_DIR "${BOOST_ROOT}/include")

add_library(UdpSocketPlugin SHARED
    include/UdpSocket.h
    include/BoostUdpSocket.h
    src/UdpSocket.cpp
    src/BoostUdpSocket.cpp
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
    ${BOOST_INCLUDE_DIR}
)

target_link_libraries(UdpSocketPlugin PRIVATE AxInterface ws2_32)
target_compile_definitions(UdpSocketPlugin PRIVATE WIN32_LEAN_AND_MEAN BOOST_ALL_NO_LIB _WIN32_WINNT=0x0601)

install(TARGETS UdpSocketPlugin
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)

