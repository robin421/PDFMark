# PDFMark - 跨平台 PDF 固化水印工具

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](.github/workflows/windows-build.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/std/the-standard)
[![Qt 6](https://img.shields.io/badge/Qt-6.6-green.svg)](https://www.qt.io)
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows%20x64-lightgrey)](#构建)

> 将水印**彻底烧录**进 PDF 的每一页像素层——既抗 OCR 提取，又消除原始矢量/文本层隐患。

## 背景

普通水印工具会在 PDF 之上叠加一个独立的水印对象，攻击者只要用 PDF 编辑器选中删除即可剥离。
**PDFMark** 采用**光栅化重编码**方案：每一页先通过 PDFium 以 200 DPI（默认）光栅化为 RGB 位图，再在像素层通过 QPainter 绘制浅灰斜向水印并按 JPEG 压缩，最终将整张图重新嵌入新 PDF 页面，输出文件**不包含任何独立的水印矢量/文本对象**。

## 核心特性

| 特性 | 说明 |
| --- | --- |
| 🛡 **固化输出** | 输出 PDF 无独立水印对象，抗 PDF 编辑器删除与文本复制 |
| ⚡ **O(1) 内存流式处理** | 单页处理完毕立即释放 QImage / FPDF_PAGE，1000+ 页不爆内存 |
| 🎨 **可调水印参数** | 文字、字号、透明度、旋转角度、错位排布、边界外扩防留白 |
| 🔐 **密码保护 PDF** | 加密输入自动弹窗输入口令；已知口令自动复用 |
| 🧵 **多模式并发** | 低 / 标准 / 极速三档，按机器资源自动分配线程数 |
| 📦 **绿色免安装** | 解压后双击 `PdfMark.exe` 直接运行，不写注册表，无安装包，绝无需重启系统 |
| 🧪 **自动化测试** | 6 个单元与视觉回归测试，CI 全绿 |

### 通用前置依赖

- **Qt 6.6+** (Core, Gui, Widgets, Test)
- **CMake 3.20+**
- **C++20 编译器**：AppleClang 15+ / MSVC 2022 / GCC 11+
- **PDFium**：`bblanchon/pdfium-binaries` chromium/8035 标签
- **macOS**：`brew install qt cmake ninja`
- **Windows**：MSVC 2022 + Qt 6.6 (`win64_msvc2019_64`) + Ninja

### 一键构建（macOS）

```bash
# 准备 PDFium（已放置 third_party/pdfium/ 时跳过）
# 准备 Qt 6：brew install qt

cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"

cmake --build build -j
```

### Windows 系统要求

- **推荐**：Windows 10/11 x64
- **兼容**：Windows 7 SP1 x64（需预先安装以下两个微软补丁，否则程序会报错无法启动）
  - [KB2670838](https://www.microsoft.com/zh-cn/download/details.aspx?id=36843)（DirectX 11 软件光栅器更新）— 提供 `CreateDXGIFactory2` 等 dxgi.dll 新增接口
  - [KB2999226](https://www.microsoft.com/zh-cn/download/details.aspx?id=49077)（Universal C Runtime）
  - 安装后**必须重启电脑**，再运行 `PdfMark.exe`

### Windows 构建（需 Windows 10 SDK）

### 运行测试

```bash
ctest --test-dir build --output-on-failure
```

### 打包 Windows 发行版

```powershell
.\scripts\package-windows.ps1 -BuildDir build -OutputDir dist -QtDir "C:/Qt/6.6.3/msvc2019_64"
```

产物：`dist/PDFMark-Windows-x64.zip`（含所有依赖，完全免安装，无需管理员权限，解压即用）。

## 使用方法

1. 启动 `PdfMark`：
   - **macOS**：双击 `./build/PdfMark.app`。
   - **Windows**：解压 `PDFMark-Windows-x64.zip` 后**直接双击 `PdfMark.exe`** 即可使用（**绿色免安装版，切勿寻找或安装任何安装包，无需重启计算机**）。
2. 拖拽 PDF 文件/文件夹到主窗口，或点击「添加 PDF 文件...」。
3. 调整水印参数：
   - **文字**、**字号**、**透明度**（1–100%）、**旋转角度**（默认 -35°）
   - **渲染清晰度**（150 / 200 / 300 DPI）
   - **压缩质量**（JPEG 质量 20–100）
   - **性能模式**：低（单线程）/ 标准 / 极速（全核并发）
4. 右侧预览面板实时显示 A4 灰度效果。
5. 点击「生成所选」或「生成全部」。任务状态在底部进度条实时更新。

### 输出命名

- 默认输出在源文件同目录，文件名加 `_watermarked.pdf` 后缀。
- 选择自定义输出目录后，所有文件统一写入该目录。

## 崩溃与日志排查

PDFMark 内置了 Windows 崩溃捕获与诊断日志：

1. **崩溃转储（Minidump）**：如果运行时发生未捕获异常，程序会自动在系统的 `Documents`（我的文档）目录下生成 `PDFMark_crash.dmp`，并弹出提示。
2. **诊断日志**：程序所有运行日志会自动追加记录至 `%APPDATA%\PDFMark\pdfmark.log`。
3. **排查与定位**：只需将 `PDFMark_crash.dmp` 与 `pdfmark.log` 提交至 [GitHub Issues](https://github.com/robin421/PDFMark/issues)，即可通过 Visual Studio / WinDbg 100% 精确定位到具体崩溃的函数和行号。
## 内存模式验证

`tests/MemoryModelTest.cpp` 模拟连续 100 页 A4@200DPI 处理：
- 单页缓冲 ≈ 15 MB
- 处理完毕立即释放
- 100 页后总占用增长 < 300 MB（验证无内存泄漏；线性泄漏将导致 ~1.5 GB 增长）

## CI/CD

- **`.github/workflows/windows-build.yml`**：每次 push/PR 自动在 windows-2022 + MSVC 2022 + Qt 6.6 上构建、运行测试并打包 Windows Artifact。
- **`.github/workflows/release.yml`**：推送 `v*` 标签自动多平台构建（Windows x64 + macOS arm64）并发布至 GitHub Releases。

## 项目结构

```
PDFMark/
├── CMakeLists.txt                # 项目根 CMake
├── src/
│   ├── common/Common.h           # 类型与工具
│   ├── pdf/                      # PDFium RAII 封装 (Library/Document/Renderer/Writer)
│   ├── watermark/                # 水印布局与渲染算法
│   ├── task/                     # 流式处理管线与并发调度
│   ├── diagnostics/              # 跨平台诊断
│   └── ui/                       # Qt 6 界面 (MainWindow / PasswordDialog / Preview)
├── tests/                        # 单元与视觉测试
├── scripts/
│   └── package-windows.ps1       # Windows 打包脚本
├── .github/workflows/            # CI 工作流
└── third_party/pdfium/           # PDFium 二进制库（可选，CMake 自动 FetchContent）
```

## 贡献

提交 PR 前请确认：

1. `ctest` 全绿
2. 不引入 new dependency 用于一两行能解决的功能
3. 命名风格与现有代码保持一致
4. 重大行为变更请同步更新 `README.md`

## 许可证

MIT。详见 [LICENSE](LICENSE)。

PDFium 遵循其 [BSD-3 许可证](https://pdfium.googlesource.com/pdfium/+/main/LICENSE)。
