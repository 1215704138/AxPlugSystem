# LoggerService CMakeLists.txt - v2

add_library(LoggerPlugin SHARED
    include/LoggerService.h
    src/LoggerService.cpp
    src/module.cpp
)

# 插件导出宏
setup_plugin_target(LoggerPlugin)

# 输出目录
set_target_properties(LoggerPlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

target_include_directories(LoggerPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/core/LoggerService
)

# --- spdlog 依赖查找 ---
set(SPDLOG_INSTALL_DIR "${CMAKE_SOURCE_DIR}/deps/spdlog")
set(SPDLOG_INCLUDE_DIR "${SPDLOG_INSTALL_DIR}/include")

if(EXISTS "${SPDLOG_INCLUDE_DIR}/spdlog")
    message(STATUS "[LoggerService] Found header-only spdlog at ${SPDLOG_INSTALL_DIR}")
    if(NOT TARGET spdlog)
        add_library(spdlog INTERFACE)
        target_include_directories(spdlog INTERFACE "${SPDLOG_INCLUDE_DIR}")
        target_compile_definitions(spdlog INTERFACE SPDLOG_HEADER_ONLY)
    endif()
else()
    message(WARNING "[LoggerService] spdlog headers not found at ${SPDLOG_INSTALL_DIR}")
endif()

# 插件只需要头文件和 spdlog
target_link_libraries(LoggerPlugin PRIVATE AxInterface spdlog)

install(TARGETS LoggerPlugin
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)

