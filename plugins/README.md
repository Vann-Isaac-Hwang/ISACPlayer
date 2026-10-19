# ISACPlayer Plugins

运行时插件放在可执行文件旁边的 `plugins/` 目录中：

```text
ISACPlayer
plugins/
  libReplayGainPlugin.so
```

插件 SDK 位于 `plugin_sdk/include/isac/plugin/`。DSP 插件只需要依赖
`isac/plugin/IAudioPlugin.h`，Qt 动态发现边界额外包含
`isac/plugin/QtAudioPlugin.h`，并使用 `Q_PLUGIN_METADATA` 和
`Q_INTERFACES(isac::IAudioPlugin)` 导出。

一个独立插件的最小 CMake 依赖是：

```cmake
add_subdirectory(path/to/ISACPlayer/plugin_sdk)
target_link_libraries(MyPlugin PRIVATE isac_plugin_sdk Qt6::Core)
```

插件的 `createNode()` 每次必须返回一个新的 `std::shared_ptr<IAudioNode>`。
节点的 `process()` 运行在实时音频线程中，不能分配内存、加锁、访问文件或调用
Qt 信号槽。插件的 UI 控件通过 `createControlWidget()` 提供，不能从 DSP 节点反向
访问 Qt 对象。

当前插件 ABI 版本：

```text
org.isacplayer.IAudioPlugin/1.0
```

SDK 的 DSP 接口不依赖 Qt。插件控制面板属于后续独立的 Qt 扩展层，不能从
实时 `process()` 线程访问 Qt 对象。

