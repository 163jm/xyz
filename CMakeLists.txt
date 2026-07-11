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
# 用户自行解压 mpv-dev-x86_64 到此目录
set(MPV_ROOT "${CMAKE_SOURCE_DIR}/third_party/mpv")
if(NOT EXISTS "${MPV_ROOT}/include/mpv/client.h")
    message(WARNING
        "third_party/mpv/include/mpv/client.h 未找到。\n"
        "请将 mpv-dev-x86_64-*.7z 解压到 third_party/mpv/，"
        "确保 third_party/mpv/include/mpv/client.h、mpv.lib、mpv-2.dll 存在。")
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

# ---- 拷贝 mpv-2.dll 到输出目录 ----
add_custom_command(TARGET MEPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${MPV_ROOT}/mpv-2.dll"
        $<TARGET_FILE_DIR:MEPlayer>
    COMMENT "Copy mpv-2.dll to output directory"
)

# ---- 安装 ----
install(TARGETS MEPlayer RUNTIME DESTINATION bin)
install(FILES "${MPV_ROOT}/mpv-2.dll" DESTINATION bin)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/icons DESTINATION assets)
