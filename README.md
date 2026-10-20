# ISACPlayer

ISACPlayer 是一个面向本地音乐播放与实时音频处理的 Qt 工程，结合了 Apple Music 风格的曲库界面和 Coppwr 风格的 DSP 节点连线工作区。

当前工程已经具备：

- 基于 DAG 的实时音频处理引擎
- 64 位 `double` planar 音频总线
- Qt Widgets 曲库与 DSP 节点实验室
- TagLib 音乐标签和封面扫描
- 可添加、连接、配置和移除多个 DSP 节点
- 基于 `QPluginLoader` 的独立动态插件加载
- Gain、Adder、ReplayGain、Biquad Equalizer 插件
- 插件 Inspector 控件与实时参数更新
- SQLite 曲库 Repository，支持稳定曲目 ID、搜索排序、扫描目录记录和歌单数据
- 稳定曲目 ID 播放队列，支持顺序、随机、单曲循环和列表循环
- 曲库可搜索歌曲/艺人/专辑，并按加入时间、标题、艺人或专辑排序

## 目录结构

```text
ISACPlayer/
├── CMakeLists.txt              # 主工程与测试配置
├── main.cpp                    # 程序入口
├── mainwindow.cpp/.h           # 主窗口与页面导航
├── src/
│   ├── engine/graph/           # DAG 图引擎、音频缓冲区与图编译
│   ├── library/                # 音乐文件和 TagLib 元数据扫描
│   └── ui/                     # 曲库、Patchbay、插件库和 Inspector
├── plugin_sdk/
│   ├── include/isac/plugin/    # 外部 DSP 插件 SDK
│   └── CMakeLists.txt
├── plugins/
│   ├── gain/                   # 增益插件
│   ├── adder/                  # 双输入加法插件
│   ├── replaygain/             # ReplayGain 插件
│   ├── biquad/                 # Biquad 参数均衡器插件
│   └── README.md               # 插件开发约定
├── tests/                      # 引擎和动态插件测试
├── doc/
│   ├── MyDesign.md             # 设计规范
│   └── 交接文档.md              # 工程架构与后续计划
└── third_party/miniaudio/      # 音频后端依赖
```

## 环境要求

- CMake 3.16 或更高版本
- C++20 编译器
- Qt 5 或 Qt 6，包含 `Core` 和 `Widgets`
- TagLib，以及可被 `pkg-config` 找到的 `taglib` 模块
- Linux、macOS 或 Windows

Linux 下可以先安装对应发行版的 Qt、TagLib、CMake 和编译器开发包。工程通过 `pkg-config` 查找 TagLib：

```bash
pkg-config --modversion taglib
```

## 构建

在工程根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
```

构建完成后，主程序通常位于：

```text
build/ISACPlayer
```

插件会自动生成到：

```text
build/plugins/
├── libisac_gain.so
├── libisac_adder.so
├── libisac_replaygain.so
└── libisac_biquad.so
```

在 macOS 或 Windows 上，插件文件扩展名会分别使用平台对应的动态库格式。

## 运行

程序启动时会扫描可执行文件旁边的 `plugins/` 目录：

```text
运行目录/
├── ISACPlayer
└── plugins/
    ├── libisac_gain.so
    ├── libisac_adder.so
    ├── libisac_replaygain.so
    └── libisac_biquad.so
```

直接运行构建目录中的程序：

```bash
./build/ISACPlayer
```

如果使用 Qt Creator，请确保运行配置的工作目录或部署目录中存在 `plugins/` 子目录。开发构建通常已经由 CMake 将插件输出到 `build/plugins/`。

## 基本使用

1. 启动 ISACPlayer。
2. 在音乐资料库中添加或扫描音乐目录。
3. 切换到“DSP 节点实验室”。
4. 从右侧“DSP 模块库”选择插件并实例化到画布。
5. 拖动节点端口建立音频连接。
6. 选中节点，在 Inspector 中调整插件参数。
7. 可在画布中移除用户创建的节点及其连接。

DSP 模块可以重复实例化。插件是否显示配置面板取决于插件是否实现了可选的 Qt Inspector 接口。

## 音乐资料库

曲库使用 SQLite 持久化，数据库位于 Qt `AppDataLocation` 下的
`library.sqlite`。扫描任务在后台线程中读取 TagLib 元数据，并由该线程独占自己的
SQLite 连接；目录按文件逐个遍历，未变化的文件根据大小和修改时间跳过，数据库写入按
64 首一批提交。曲目表格通过分页查询读取数据，不会将整个曲库常驻内存。

## 插件架构

插件由两部分组成：

1. `IAudioPlugin`：纯 C++ 插件工厂和元数据接口。
2. `IAudioPluginUi`：可选的 Qt 配置面板接口。

SDK 头文件位于：

```text
plugin_sdk/include/isac/plugin/
```

插件使用 Qt 动态发现接口，并通过 `Q_PLUGIN_METADATA` 与 `Q_INTERFACES` 导出。宿主程序使用 `QPluginLoader` 扫描运行目录下的动态库，读取插件元数据并创建节点。

一个插件的基本 CMake 结构如下：

```cmake
add_library(my_plugin MODULE
    MyPlugin.cpp
    MyPlugin.h
    MyNode.h
)

target_link_libraries(my_plugin PRIVATE
    isac_plugin_sdk
    Qt${QT_VERSION_MAJOR}::Widgets
)

set_target_properties(my_plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)
```

更完整的插件约定见 [plugins/README.md](plugins/README.md)。

## 实时 DSP 约束

所有插件节点的 `process()` 都可能运行在实时音频线程中，必须遵守以下规则：

- 音频总线使用 `double` 样本和 planar channel layout。
- 不在 `process()` 中进行 `new`、`delete`、`malloc` 或其他动态分配。
- 不加互斥锁，不等待条件变量，不执行文件或网络 I/O。
- 不访问 Qt UI、Qt 信号槽或其他非实时对象。
- UI 参数通过原子变量或其他无锁控制方式传递到音频线程。
- 参数更新需要对异常值进行限制，避免产生 `NaN` 或无穷大。
- 插件创建的节点必须是独立实例，不能在不同画布节点之间共享处理状态。

插件动态库由宿主保持加载，确保节点销毁前插件代码和虚函数表仍然有效。

## 测试

运行全部测试：

```bash
ctest --test-dir build --output-on-failure
```

当前测试覆盖：

- 64 位 planar 音频缓冲区
- Biquad bypass、参数更新和有限值输出
- DAG 节点添加、连接和移除
- 所有内置动态插件的 `QPluginLoader` 加载
- 插件节点的基本音频处理
- SQLite 曲目持久化、分页查询、搜索排序、过滤行号和歌单顺序
- 后台目录扫描、未变化文件跳过及已修改文件更新
- 播放队列的顺序、随机、单曲循环和列表循环边界

也可以单独执行：

```bash
./build/isac_engine_tests
./build/isac_plugin_loader_tests \
    ./build/plugins/libisac_replaygain.so \
    ./build/plugins/libisac_gain.so \
    ./build/plugins/libisac_adder.so \
    ./build/plugins/libisac_biquad.so
```

## 当前状态与后续方向

当前工程重点已经从内置 DSP 实现迁移到独立插件架构。后续工作包括：

- 实现艺人、专辑视图与歌单管理界面
- 完善歌单创建、重命名、曲目增删和歌单播放交互
- 接入 KDE MPRIS D-Bus 系统媒体控制
- 增加 ReplayGain 跨 block lookahead 的专项测试
- 将旧内置 DSP 测试完全迁移到动态插件测试
- 继续优化超大音乐库扫描的内存占用
- 完善插件安装、部署和版本兼容检查
- 增加插件参数 schema 与自动化 Inspector
- 在 ABI 稳定后评估跨编译器环境的 C ABI 工厂层

设计原则和完整交接信息请参阅：

- [doc/MyDesign.md](doc/MyDesign.md)
- [doc/交接文档.md](doc/交接文档.md)
- [plugins/README.md](plugins/README.md)
