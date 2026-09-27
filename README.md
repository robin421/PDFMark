# PDFMark - 跨平台 PDF 固化水印工具

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](.github/workflows/windows-build.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/std/the-standard)
[![Qt 6](https://img.shields.io/badge/Qt-6.2-green.svg)](https://www.qt.io)
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows%20x64-lightgrey)](#构建)

> 将水印**彻底烧录**进 PDF 的每一页像素层——既抗 OCR 提取，又消除原始矢量/文本层隐患。

## 背景

普通水印工具会在 PDF 之上叠加一个独立的水印对象，攻击者只要用 PDF 编辑器选中删除即可剥离。
**PDFMark** 采用**光栅化重编码**方案：每一页先通过 PDFium 以 200 DPI（默认）光栅化为 RGB 位图，再在像素层通过 QPainter 绘制浅灰斜向水印并按 JPEG 压缩，最终将整张图重新嵌入新 PDF 页面，输出文件**不包含任何独立的水印矢量/文本对象**。

## 核心特性

| 特性 | 说明 |
| --- | --- |
| 🛡 **固化输出** | 输出 PDF 无独立水印对象，抗 PDF 编辑器删除与文本复制 |
| ⚡ **流式页处理** | 单页位图处理完毕立即释放（O(1)/页）；输出文档在内存中累积，故并发受内存预算约束 |
| 🧮 **内存预算与自动降速** | 启动前估算峰值内存，自动限制并发文档数、分批处理并在必要时降速，避免大任务 OOM 崩溃 |
| 🎨 **可调水印参数** | 文字、字号、透明度、旋转角度、错位排布、边界外扩防留白 |
| 🧩 **水印模板复用** | 将「多行水印文字 + 一套样式」存为模板，一键套用到全部 PDF 后批量生成 |
| 🔐 **密码保护 PDF** | 加密输入自动弹窗输入口令；已知口令自动复用 |
| 🧵 **多模式并发** | 低 / 标准 / 极速三档；PDFium 调用已全局串行（见下），并发用于重叠 JPEG 编码与磁盘 I/O |
| 📦 **绿色免安装** | 解压后双击 `PdfMark.exe` 直接运行，不写注册表，无安装包，绝无需重启系统 |
| 🧪 **自动化测试** | 12 个单元与视觉回归测试，CI 全绿 |

### 通用前置依赖

- **Qt 6.2+** (Core, Gui, Widgets, Test) — Windows 打包使用 6.2.4 LTS，兼容 Win7 SP1
- **C++20 编译器**：AppleClang 15+ / MSVC 2022 / GCC 11+
- **PDFium**：`bblanchon/pdfium-binaries` chromium/8035 标签
- **macOS**：`brew install qt cmake ninja`
- **Windows**：MSVC 2022 + Qt 6.2 (`win64_msvc2019_64`) + Ninja
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
   - **性能模式**：保守（1 个文件）/ 日常推荐（2 个）/ 火力全开（最多 4 个）
4. 右侧预览面板实时显示 A4 灰度效果。
5. 点击「生成所选」或「生成全部」。任务状态在底部进度条实时更新。

### 水印模板（复用文字 + 样式）

同一个水印文字与样式要反复用于多批 PDF 时，无需重复填写：

1. 在某一个 PDF 上配置好水印文字（可多行）与右侧「水印样式设置」，点击 **「存为模板...」** 并命名。
2. **「应用」**：把所选模板套用到当前选中的 PDF。
3. **「应用到全部文件」**：一次性套用到左侧列表中的所有 PDF。
4. 点击 **「生成全部」** 即完成批量固化（模板只是配置来源，生成链路不变）。
5. **「管理...」** 中可重命名或删除模板。

模板保存在用户应用数据目录下的 `watermark_templates.json`：

- **macOS**：`~/Library/Application Support/PDFMark/PDFMark/watermark_templates.json`
- **Windows**：`%APPDATA%\PDFMark\PDFMark\watermark_templates.json`

不写注册表，保持「绿色免安装」；删除该文件即可清空所有模板。

### 大批量任务与内存保护

「生成全部」处理很多文件时，如果在无约束的情况下同时开工，每个并发文档都会同时持有：源 PDF 全文、当前页位图（200 DPI A4 ≈ 15 MB，300 DPI ≈ 35 MB，另加一份工作副本），以及**整份输出文档**（所有已压缩页面，直到该文档保存为止）。并发数一多就可能耗尽内存。

因此程序会：

1. **启动前预检**：当文件数 > 20、处理页数 > 200，或内存风险评估非「安全」时，弹出资源预检对话框，显示文件数、水印条数、预计页数、单文件峰值与可用内存，并给出**推荐并发数**。
   - 默认按钮为「按推荐设置继续」；
   - 风险为「风险」时默认按钮为「取消」，「按当前设置继续」需二次确认；
   - 可勾选「本次会话不再提示」。
2. **有界滑动窗口**：无论排队多少文件，任何时刻在飞的文档数都不会超过并发上限。**峰值内存取决于并发上限，与文件总数无关。**
3. **按需降速**：当批次远大于并发窗口时，在两次提交之间插入短暂间隔，让机器与界面保持响应。

并发上限 = min(性能模式允许的并发, 内存预算允许的并发)：
- 内存预算 = min(物理内存 × 50%, 物理内存 − 当前占用 − 512 MB 保留)，下限 64 MB；
- 单文档峰值 ≈ 源 PDF 大小 + 2 × 页面位图 + 页数 × 水印数 × ~0.4 MB。

当前使用的并发数与限速值会显示在底部状态栏。

### 关于线程安全（为什么并发不再是「全核并发」）

PDFium 官方头文件 `third_party/pdfium/include/fpdfview.h` 明确声明：

> None of the PDFium APIs are thread-safe. They expect to be called from a single thread.
> Barring that, embedders are required to ensure (via a mutex or similar) that only a
> single PDFium call can be made at a time.

早期版本从多个线程池线程并发调用 PDFium，实测在文件中包含复杂颜色/内容流时会有约 10%+ 概率崩溃（`EXC_BAD_ACCESS`，栈落在 `CPDF_ContentParser` / `CPDF_ColorState` / `CPDF_ReadValidator`）——这正是「PDF 一多、点生成全部就崩」的根因。

现在所有 PDFium 调用都由 `PdfLibrary::callMutex()`（全局递归互斥锁）串行保护，因此：

- **正确性优先**：不会再随机崩溃；
- 并发线程只用于**重叠 Qt 侧工作**（水印绘制、JPEG 编码、磁盘 I/O），不是全核并行光栅化；
- 性能模式档位：保守 = 1 个文件，日常推荐 = 2 个，火力全开 = 最多 4 个（受内存预算进一步限制）。

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

## 修复记录

- **修复「删除选中文件」多删 Bug**：文件表格为 4 列且启用了整行选中（`SelectRows`），
  原实现直接遍历 `QTableWidget::selectedItems()`，每个选中行会返回 4 个单元格条目，
  去重缺失导致删除时同一行号被重复处理，从而连带删除后面的文件（例如选中 1 个文件会删掉 4 个）。
  现改为通过 `QTableWidget::selectionModel()->selectedRows()` 获取去重后的行号（封装在
  `src/ui/FileTableSelection.h`），并修正删除后的选中锚点与状态栏计数。新增回归测试
  `tests/FileRemovalTest.cpp`（`ctest -R FileRemovalTest`）。

## 贡献

提交 PR 前请确认：

1. `ctest` 全绿
2. 不引入 new dependency 用于一两行能解决的功能
3. 命名风格与现有代码保持一致
4. 重大行为变更请同步更新 `README.md`

## 许可证

MIT。详见 [LICENSE](LICENSE)。

PDFium 遵循其 [BSD-3 许可证](https://pdfium.googlesource.com/pdfium/+/main/LICENSE)。
