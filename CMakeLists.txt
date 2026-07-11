cmake_minimum_required(VERSION 3.20)
project(MEPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---- Windows 平台设置 ----
if(WIN32)
    add_compile_definitions(
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00  # Windows 10
    )
    if(MSVC)
        add_compile_options(/W3 /utf-8 /permissive-)
        # 静态链接 CRT，避免依赖 vcruntime
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# ---- 依赖查找 ----
# TagLib / SQLite3 / libcurl 通过 vcpkg 提供
find_package(TagLib REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)

# mpv：third_party/mpv/ 下放置 mpv.lib / mpv-2.dll / include/mpv/*.h
# 用户自行解压 mpv-dev-x86_64 到此目录。
# 注意：shinchiro 的 mpv-dev 包不含 MSVC 的 mpv.lib，只有 mpv.def。
# 下面自动检测：若 mpv.lib 不存在但 mpv.def 存在，用 MSVC lib.exe 生成。
set(MPV_ROOT "${CMAKE_SOURCE_DIR}/third_party/mpv")
if(NOT EXISTS "${MPV_ROOT}/include/mpv/client.h")
    message(WARNING
        "third_party/mpv/include/mpv/client.h 未找到。\n"
        "请将 mpv-dev-x86_64-*.7z 解压到 third_party/mpv/，"
        "确保 third_party/mpv/include/mpv/client.h、mpv-2.dll、mpv.def 存在。")
endif()

# 若缺 mpv.lib 但有 mpv.def，自动用 lib.exe 生成（需 MSVC 环境）
if(NOT EXISTS "${MPV_ROOT}/mpv.lib" AND EXISTS "${MPV_ROOT}/mpv.def")
    message(STATUS "mpv.lib 不存在，从 mpv.def 生成...")
    execute_process(
        COMMAND lib /def:${MPV_ROOT}/mpv.def /name:mpv-2.dll
                /out:${MPV_ROOT}/mpv.lib /MACHINE:X64
        RESULT_VARIABLE _lib_result
        OUTPUT_VARIABLE _lib_out
        ERROR_VARIABLE _lib_err
    )
    if(NOT _lib_result EQUAL 0)
        message(WARNING "从 mpv.def 生成 mpv.lib 失败（可能未在 MSVC 开发者命令提示符下运行）。\n"
                        "请手动在「x64 Native Tools Command Prompt」中执行：\n"
                        "  cd third_party\\mpv\n"
                        "  lib /def:mpv.def /name:mpv-2.dll /out:mpv.lib /MACHINE:X64\n"
                        "错误: ${_lib_err}")
    else()
        message(STATUS "mpv.lib 生成成功")
    endif()
endif()

# ---- 资源 ----
# Windows 应用图标（.rc 引用 .ico）
configure_file(${CMAKE_SOURCE_DIR}/assets/icons/app_icon.ico
               ${CMAKE_BINARY_DIR}/assets/app_icon.ico COPYONLY)

# 资源头文件（包含程序图标资源）
set(APP_RC ${CMAKE_SOURCE_DIR}/src/app.rc)

# ---- 源文件收集 ----
file(GLOB_RECURSE MEPLAYER_SOURCES
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)
# 排除 main 单独处理
list(REMOVE_ITEM MEPLAYER_SOURCES "${CMAKE_SOURCE_DIR}/src/main.cpp")

# ---- 可执行目标 ----
add_executable(MEPlayer WIN32
    ${CMAKE_SOURCE_DIR}/src/main.cpp
    ${MEPLAYER_SOURCES}
    ${APP_RC}
)

target_include_directories(MEPlayer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${MPV_ROOT}/include
)

target_link_libraries(MEPlayer PRIVATE
    ${MPV_ROOT}/mpv.lib
    TagLib::TagLib
    SQLite::SQLite3
    CURL::libcurl
    # Windows 系统库
    d3d11
    dxgi
    d2d1
    dwrite
    d3dcompiler
    windowscodecs    # WIC
    mf                # Media Foundation
    mfplat
    mfreadwrite
    mfuuid
    strmiids         # Codec API
    propsys          # Property System
    shlwapi
    shell32
    user32
    gdi32
    comctl32
    ole32
    oleaut32
    uuid
    dwmapi
    ws2_32
    winmm
    winhttp         # IPTV M3U 下载
    comdlg32        # GetOpenFileName 文件选择
)

# ---- 拷贝 mpv dll 到输出目录（shinchiro 的 dll 名为 libmpv-2.dll）----
file(GLOB MPV_DLLS "${MPV_ROOT}/*.dll")
foreach(_dll ${MPV_DLLS})
    add_custom_command(TARGET MEPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_dll}" $<TARGET_FILE_DIR:MEPlayer>
        COMMENT "Copy ${_dll} to output directory"
    )
endforeach()

# ---- 安装 ----
install(TARGETS MEPlayer RUNTIME DESTINATION bin)
foreach(_dll ${MPV_DLLS})
    install(FILES "${_dll}" DESTINATION bin)
endforeach()
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/icons DESTINATION assets)
