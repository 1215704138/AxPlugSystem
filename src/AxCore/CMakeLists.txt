# AxCore CMakeLists.txt - v2 plugin system core

# 生成动态库（用于运行时跨模块单例）
add_library(AxCore SHARED
    AxPluginManager.cpp
    AxCoreDll.cpp
)

# 动态库配置
target_compile_definitions(AxCore PRIVATE AX_CORE_EXPORTS)
target_link_libraries(AxCore PUBLIC AxInterface)

# 统一输出到 bin 目录 (让 VS 自动处理 Debug/Release 子目录)
set_target_properties(AxCore PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Windows特定设置
if(WIN32)
    target_link_libraries(AxCore PRIVATE 
        kernel32
        user32
        shlwapi
    )
    
    # 为AxCore SHARED创建.def文件
    set_target_properties(AxCore PROPERTIES
        LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/AxCoreDll.def"
    )
endif()

# 安装目标
install(TARGETS AxCore
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)
