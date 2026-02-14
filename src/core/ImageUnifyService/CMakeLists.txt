# ImageUnifyService CMakeLists.txt - v3 (simplified)

add_library(ImageUnifyServicePlugin SHARED
    include/ImageUnifyService.h
    src/ImageUnifyService.cpp
    src/module.cpp
)

# 插件导出宏
setup_plugin_target(ImageUnifyServicePlugin)

# 输出目录
set_target_properties(ImageUnifyServicePlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

target_include_directories(ImageUnifyServicePlugin PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/core/ImageUnifyService
)

# 插件核心只需要接口头文件, OpenCV是可选的
target_link_libraries(ImageUnifyServicePlugin PRIVATE AxInterface)
target_compile_definitions(ImageUnifyServicePlugin PRIVATE WIN32_LEAN_AND_MEAN)

install(TARGETS ImageUnifyServicePlugin
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)

