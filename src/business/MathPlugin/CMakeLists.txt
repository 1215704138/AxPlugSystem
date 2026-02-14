# MathPlugin CMakeLists.txt - v2

add_library(MathPlugin SHARED
    include/MathPlugin.h
    src/MathPlugin.cpp
    src/module.cpp
)

# 插件导出宏
setup_plugin_target(MathPlugin)

# 输出目录
set_target_properties(MathPlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

target_include_directories(MathPlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/business/MathPlugin
)

# 插件只需要头文件，不需要链接 AxCore
target_link_libraries(MathPlugin PRIVATE AxInterface)

install(TARGETS MathPlugin
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)


