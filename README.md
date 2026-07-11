# MEPlayer

原生 Windows 媒体播放器，C++ + libmpv + Direct2D/DirectWrite 自绘 UI，复刻 Flutter Material 3 风格。

由 [flutterplayer](https://github.com/163jm/flutterplayer) Windows 端改写而来，UI 与操作逻辑完全一致。

## 技术栈

| 层 | 技术 |
|---|---|
| 语言 | C++17 |
| GUI | Win32 + Direct2D / DirectWrite（自绘控件库） |
| 播放器 | libmpv（D3D11 渲染） |
| 元数据 | TagLib（音乐）/ Media Foundation（视频缩略图） |
| 持久化 | SQLite |
| 网络 | WinHTTP（IPTV M3U 下载） |
| 构建 | CMake + vcpkg + MSVC |

## 功能

- **视频**：本地视频库（扫描 + 缩略图网格 + 播放列表）、网络视频
- **IPTV**：M3U 源管理（远程/文件）、三列频道播放页（分组/频道/源）
- **音乐**：本地音乐库（TagLib 元数据、分类/排序、封面按需内存缓存 200+200 增量）、歌词同步、播放队列
- **设置**：主题模式（跟随系统/浅色/深色）、6 种种子色、mpv 三档画质预设 + 硬解/去交错/TS容错/字幕开关
- **系统**：托盘最小化、关闭拦截、全屏、键盘快捷键

## 构建

### 环境要求

- Visual Studio 2022（含 C++ 桌面开发）
- CMake 3.20+
- vcpkg

### 依赖安装

```powershell
vcpkg install taglib sqlite3 curl
```

### 放置 mpv 开发库

从 [mpv-winbuild-cmake releases](https://github.com/shinchiro/mpv-winbuild-cmake/releases) 下载 `mpv-dev-x86_64-*.7z`（注意不要下 `-v3-` 变体），解压到 `third_party/mpv/`，确保包含：

```
third_party/mpv/
├── include/mpv/client.h
├── include/mpv/render.h
├── include/mpv/render_d3d11.h
├── mpv-2.dll        # 运行时
├── mpv.def          # 模块定义文件
└── (libmpv.dll.a)   # MinGW import library，MSVC 不用
```

注意：shinchiro 的 mpv-dev 包**不含 MSVC 的 `mpv.lib`**。CMake 配置时会自动用 MSVC `lib.exe` 从 `mpv.def` 生成。若自动生成失败（未在 MSVC 环境下），手动在「x64 Native Tools Command Prompt」执行：

```cmd
cd third_party\mpv
lib /def:mpv.def /name:mpv-2.dll /out:mpv.lib /MACHINE:X64
```

### 编译

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

### 运行

`build/Release/MEPlayer.exe` + `mpv-2.dll` 同目录分发。

## 目录结构

```
meplayer/
├── CMakeLists.txt
├── third_party/mpv/          # mpv 开发库（用户自行解压）
├── assets/icons/             # 程序图标（与原项目一致）
├── src/
│   ├── main.cpp              # WinMain 入口
│   ├── app/                  # 应用层（窗口/托盘/设置/主题/全局播放器）
│   ├── core/                 # 核心层（mpv/元数据/扫描/缓存/存储）
│   ├── ui/                   # Direct2D 自绘控件库
│   ├── modules/              # 业务模块（home/video/iptv/music/settings）
│   └── util/                 # 工具
└── README.md
```

## 数据目录

exe 同级 `appdata/`：
- `meplayer.db` — SQLite 数据库（设置/扫描路径/IPTV源/最近播放/视频缩略图缓存）
- `thumbnails/` — 视频缩略图缓存（落盘）
- 音乐封面不落盘，按需加载到内存（前 200 首 + 滚动增量 200）

## mpv 设置

三档画质预设：
- **性能优先**：profile=fast, hwdec=auto-safe, bilinear 缩放
- **均衡**：留空（避免与渲染层冲突）
- **画质优先**：profile=gpu-hq, vo=gpu-next, ewa_lanczossharp 缩放, sigmoid-upscaling, deband

可选开关：硬件解码、去交错（IPTV）、TS 容错、字幕自动加载、字幕字号、默认音量、播放速度。
