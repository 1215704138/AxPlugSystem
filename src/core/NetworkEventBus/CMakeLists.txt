# NetworkEventBus CMakeLists.txt - Phase 4: Network Event Bus Plugin

add_library(NetworkEventBusPlugin SHARED
    NetworkEventBusImpl.cpp
    module.cpp
)

setup_plugin_target(NetworkEventBusPlugin)
set_target_properties(NetworkEventBusPlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

target_include_directories(NetworkEventBusPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(NetworkEventBusPlugin PRIVATE AxInterface AxCore ws2_32)

install(TARGETS NetworkEventBusPlugin
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)
