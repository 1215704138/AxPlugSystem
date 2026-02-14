# AxPlug v2 测试程序

# --- 核心修改：使用 Publish 文件夹作为依赖源 ---
set(PUBLISH_DIR "${CMAKE_SOURCE_DIR}/publish")
set(PUBLISH_INCLUDE_DIR "${PUBLISH_DIR}/include")
set(PUBLISH_LIB_DIR "${PUBLISH_DIR}/lib")

# 检查是否作为子项目构建 (即 AxCore 目标是否存在)
if(TARGET AxCore)
    message(STATUS "[Test] Building as part of main project. Using AxCore target directly.")
    set(AX_CORE_LIB AxCore)
    set(PUBLISH_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
    # 不需要 link_directories，因为 target_link_libraries 会自动处理
else()
    # 独立构建模式 (原逻辑)
    if(NOT EXISTS "${PUBLISH_DIR}/bin/AxCore.dll")
        message(STATUS "[Test] Publish folder not found. Skipping test targets.")
        message(STATUS "[Test] Please run 'scripts/build_publish.bat' to generate SDK first.")
        return()
    endif()
    
    set(AX_CORE_LIB AxCore)
    include_directories("${PUBLISH_INCLUDE_DIR}")
    link_directories("${PUBLISH_LIB_DIR}")
endif()

# 包含路径和库路径
# 包含路径 (兼容两种模式)
include_directories("${PUBLISH_INCLUDE_DIR}")

# --- 定义依赖查找路径 (相对于 test 目录) ---
# 注意：OpenCV/Halcon 等第三方依赖仍然在 test/deps 下
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(TEST_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps")

# --- 1. 插件体系测试 ---
add_executable(plugin_system_test src/plugin_system_test.cpp)
# 链接到 publish/lib/AxCore.lib，而不是构建目标
# 链接到 AxCore
target_link_libraries(plugin_system_test PRIVATE ${AX_CORE_LIB})
set_target_properties(plugin_system_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# --- 2. 网络插件测试 ---
add_executable(network_test src/network_test.cpp)
target_link_libraries(network_test PRIVATE ${AX_CORE_LIB})
set_target_properties(network_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# --- 3. 综合日志服务测试 ---
add_executable(logger_test src/logger_test.cpp)
target_link_libraries(logger_test PRIVATE ${AX_CORE_LIB})
set_target_properties(logger_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# --- OpenCV 依赖查找 ---
set(OPENCV_INSTALL_DIR "${TEST_DEPS_DIR}/opencv")
set(OPENCV_INCLUDE_DIR "${OPENCV_INSTALL_DIR}/include")
set(OPENCV_LIB_DIR "${OPENCV_INSTALL_DIR}/lib")

if(EXISTS "${OPENCV_LIB_DIR}/opencv_core480d.lib" OR EXISTS "${OPENCV_LIB_DIR}/opencv_core480.lib")
    message(STATUS "[Test] Found pre-compiled OpenCV at ${OPENCV_INSTALL_DIR}")
    set(OpenCV_FOUND TRUE)
    set(OpenCV_INCLUDE_DIRS "${OPENCV_INCLUDE_DIR}")
    
    # OpenCV导入库
    add_library(opencv_core STATIC IMPORTED)
    if(EXISTS "${OPENCV_LIB_DIR}/opencv_core480.lib")
        set_target_properties(opencv_core PROPERTIES
            IMPORTED_LOCATION_DEBUG "${OPENCV_LIB_DIR}/opencv_core480d.lib"
            IMPORTED_LOCATION_RELEASE "${OPENCV_LIB_DIR}/opencv_core480.lib"
            IMPORTED_LOCATION "${OPENCV_LIB_DIR}/opencv_core480d.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENCV_INCLUDE_DIR}"
        )
    else()
        set_target_properties(opencv_core PROPERTIES
            IMPORTED_LOCATION "${OPENCV_LIB_DIR}/opencv_core480d.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENCV_INCLUDE_DIR}"
        )
    endif()

    add_library(opencv_imgproc STATIC IMPORTED)
    if(EXISTS "${OPENCV_LIB_DIR}/opencv_imgproc480.lib")
        set_target_properties(opencv_imgproc PROPERTIES
            IMPORTED_LOCATION_DEBUG "${OPENCV_LIB_DIR}/opencv_imgproc480d.lib"
            IMPORTED_LOCATION_RELEASE "${OPENCV_LIB_DIR}/opencv_imgproc480.lib"
            IMPORTED_LOCATION "${OPENCV_LIB_DIR}/opencv_imgproc480d.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENCV_INCLUDE_DIR}"
        )
    else()
        set_target_properties(opencv_imgproc PROPERTIES
            IMPORTED_LOCATION "${OPENCV_LIB_DIR}/opencv_imgproc480d.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENCV_INCLUDE_DIR}"
        )
    endif()

    add_library(opencv_imgcodecs STATIC IMPORTED)
    if(EXISTS "${OPENCV_LIB_DIR}/opencv_imgcodecs480.lib")
        set_target_properties(opencv_imgcodecs PROPERTIES
            IMPORTED_LOCATION_DEBUG "${OPENCV_LIB_DIR}/opencv_imgcodecs480d.lib"
            IMPORTED_LOCATION_RELEASE "${OPENCV_LIB_DIR}/opencv_imgcodecs480.lib"
            IMPORTED_LOCATION "${OPENCV_LIB_DIR}/opencv_imgcodecs480d.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENCV_INCLUDE_DIR}"
        )
    else()
        set_target_properties(opencv_imgcodecs PROPERTIES
            IMPORTED_LOCATION "${OPENCV_LIB_DIR}/opencv_imgcodecs480d.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENCV_INCLUDE_DIR}"
        )
    endif()

    set(OpenCV_LIBS opencv_core opencv_imgproc opencv_imgcodecs)
else()
    message(WARNING "[Test] OpenCV not found at ${OPENCV_INSTALL_DIR}")
endif()

# --- 查找 Qt (可选依赖) ---
option(ENABLE_QT "Enable Qt support" ON)

if(ENABLE_QT)
    set(CMAKE_PREFIX_PATH "C:/Qt/6.2.2/msvc2019_64" ${CMAKE_PREFIX_PATH})
    find_package(Qt6 6.2.2 COMPONENTS Core Gui Widgets)
    if(Qt6_FOUND)
        message(STATUS "[Test] Found Qt6: ${Qt6_VERSION}")
    else()
        find_package(Qt5 COMPONENTS Core Gui Widgets)
        if(Qt5_FOUND)
            message(STATUS "[Test] Found Qt5: ${Qt5_VERSION}")
        else()
            message(STATUS "[Test] Qt not found")
        endif()
    endif()
endif()


# --- 4. 图像统一服务测试 ---
add_executable(image_unify_test src/image_unify_test.cpp)
target_link_libraries(image_unify_test PRIVATE ${AX_CORE_LIB})
set_target_properties(image_unify_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)


# 如果有 OpenCV, 启用 OpenCV 集成测试 (可选依赖)
if(OpenCV_FOUND)
    message(STATUS "Enabling OpenCV support in ImageUnifyTest")
    target_compile_definitions(image_unify_test PRIVATE HAS_OPENCV)
    target_link_libraries(image_unify_test PRIVATE ${OpenCV_LIBS})
else()
    message(STATUS "OpenCV not found - ImageUnifyTest will run without OpenCV features")
endif()

# 如果有 Halcon, 启用 Halcon 集成测试 (可选依赖)
set(HALCON_ROOT "${TEST_DEPS_DIR}/halcon-20.11-steady")
if(EXISTS "${HALCON_ROOT}/include/halconcpp/HalconCpp.h")
    message(STATUS "Found Halcon at: ${HALCON_ROOT}")
    target_compile_definitions(image_unify_test PRIVATE HAS_HALCON)
    target_include_directories(image_unify_test PRIVATE
        "${HALCON_ROOT}/include"
        "${HALCON_ROOT}/include/halconcpp"
    )
    target_link_directories(image_unify_test PRIVATE "${HALCON_ROOT}/lib")
    target_link_libraries(image_unify_test PRIVATE halcon halconcpp)
else()
    message(STATUS "Halcon not found - ImageUnifyTest will run without Halcon features")
endif()


# --- 自动复制 DLL 到输出目录 (Script Approach) ---
function(copy_dlls target_name)
    set(COPY_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/copy_dlls_${target_name}.cmake")
    
    # 准备文件列表 (Handling lists correctly within the script generation strings is tricky, 
    # so we replace semicolons with a special placeholder or ensure they are quoted)
    # But Cmakes set("a;b") works fine.
    
    # 1. OpenCV DLLs definition
    set(OPENCV_BIN "${TEST_DEPS_DIR}/opencv/bin")
    set(OPENCV_DEBUG_STR 
        "${OPENCV_BIN}/opencv_core480d.dll"
        "${OPENCV_BIN}/opencv_imgproc480d.dll"
        "${OPENCV_BIN}/opencv_imgcodecs480d.dll"
    )
    set(OPENCV_RELEASE_STR 
        "${OPENCV_BIN}/opencv_core480.dll"
        "${OPENCV_BIN}/opencv_imgproc480.dll"
        "${OPENCV_BIN}/opencv_imgcodecs480.dll"
    )

    # 2. Qt DLLs definition
    if(Qt6_FOUND)
         get_target_property(QT_CORE_DLL Qt6::Core LOCATION)
         get_target_property(QT_GUI_DLL Qt6::Gui LOCATION)
         get_target_property(QT_WIDGETS_DLL Qt6::Widgets LOCATION)
         set(QT_ALL_STR 
            "${QT_CORE_DLL}"
            "${QT_GUI_DLL}"
            "${QT_WIDGETS_DLL}"
         )
         # Plugins (platforms) - Derive from Core location
         get_target_property(QT_CORE_LOC Qt6::Core LOCATION)
         get_filename_component(QT_BIN_DIR "${QT_CORE_LOC}" DIRECTORY)
         set(QT_PLUGINS_DIR_STR "${QT_BIN_DIR}/../plugins")
    elseif(Qt5_FOUND)
        # Qt5 logic if needed, similar to above
    endif()

    if(DEFINED ENV{HALCONROOT})
        file(TO_CMAKE_PATH "$ENV{HALCONROOT}/bin/x64-win64" HALCON_BIN_DIR_STR)
    else()
        set(HALCON_BIN_DIR_STR "")
    endif()

    file(WRITE "${COPY_SCRIPT}" "
        message(STATUS \"Running post-build DLL copy for configuration: \${CONFIG}\")
        message(STATUS \"Target directory: \${TARGET_DIR}\")

        # 1. OpenCV
        if(\"\${CONFIG}\" STREQUAL \"Debug\")
            set(FILES \"${OPENCV_DEBUG_STR}\")
        else()
            set(FILES \"${OPENCV_RELEASE_STR}\")
        endif()
        
        foreach(f \${FILES})
            if(EXISTS \"\${f}\")
                file(COPY \"\${f}\" DESTINATION \"\${TARGET_DIR}\")
            endif()
        endforeach()

        # 2. Qt
        set(QT_FILES \"${QT_ALL_STR}\")
        foreach(f \${QT_FILES})
            if(EXISTS \"\${f}\")
                file(COPY \"\${f}\" DESTINATION \"\${TARGET_DIR}\")
            endif()
        endforeach()
        
        if(EXISTS \"\${QT_PLUGINS_DIR_STR}/platforms/qwindows.dll\")
            file(COPY \"\${QT_PLUGINS_DIR_STR}/platforms/qwindows.dll\" DESTINATION \"\${TARGET_DIR}/platforms\")
        endif()

        # 3. Halcon
        set(HALCON_DIR \"${HALCON_BIN_DIR_STR}\")
        if(HALCON_DIR AND EXISTS \"\${HALCON_DIR}/halcon.dll\")
            file(COPY \"\${HALCON_DIR}/halcon.dll\" DESTINATION \"\${TARGET_DIR}\")
            if(EXISTS \"\${HALCON_DIR}/halconcpp.dll\")
                file(COPY \"\${HALCON_DIR}/halconcpp.dll\" DESTINATION \"\${TARGET_DIR}\")
            endif()
        endif()
    ")

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DCONFIG=$<CONFIG> -DTARGET_DIR=$<TARGET_FILE_DIR:${target_name}> -P "${COPY_SCRIPT}"
    )
endfunction()

# 对 image_unify_test 应用复制
copy_dlls(image_unify_test)
