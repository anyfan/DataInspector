# QML 工具与静态检查

> **状态**：现行 · **读者**：AI 代理 / 开发者 · **关联文件**：`src/quick/qmltypes.h`、`qml/*.qml`、`qml/qmllint.ini.in` · **改 QML 或注册类型前必读**

## 检查目标

完成 CMake configure 后：

```powershell
cmake --build build_qt6-debug --target DataInspector_qmllint
```

- `MaxWarnings=0`：任何 warning 都让检查失败，不关闭警告规则。
- 覆盖全部项目 QML 文件。

## 类型元数据

- `src/quick/qmltypes.h` 用 `QML_FOREIGN` 声明 `PlotItem`、`AppController`、`SignalModel`。
- CMake 自动生成注册代码和 `.qmltypes` 到 `<构建目录>/qml/DataInspector`。
- 主程序用 `QQmlApplicationEngine::setInitialProperties` 注入控制器；测试用 `createWithInitialProperties`。
- `Main.qml` 通过 `required property AppController appController` 接收，不依赖隐式上下文变量。
- 四个 QML 文件显式 `pragma ComponentBehavior: Bound`。

> Qt 6.8 的注册模板即使对不可创建类型也会实例化 `QQmlElement<T>` 子类，因此 `AppController`/`SignalModel` 不能是 `final`。这是 Qt 限制，不是疏忽。

## 编辑器配置

CMake 从 `qml/qmllint.ini.in` 生成本地 `qml/.qmllint.ini`，包含当前构建目录的导入路径；同时启用 `.qmlls.ini` 生成。这些文件被 gitignore。

使用 VS Code/ShunCode 的 Qt QML 扩展时，如果仍报模块找不到，在工作区 `.vscode/settings.json` 加入：

```json
{
  "qt-qml.qmlls.additionalImportPaths": [
    "${workspaceFolder}/build_qt6-debug/qml"
  ]
}
```

然后执行 **Qt: Restart QML Language Server**。`.vscode/settings.json` 属于本地配置，不纳入 git。

## 边界

- QML 静态零 warning ≠ 原生 C++ 编译零 warning；Qt 自身生成代码可能有 `Q_GLOBAL_STATIC` 提示。
- 离屏测试环境可能产生字体路径/Windows 主题相关日志，这些不是 QML 绑定警告。
- 静态检查不能替代真实 GPU、鼠标操作和部署目标上的人工 GUI 验收。
