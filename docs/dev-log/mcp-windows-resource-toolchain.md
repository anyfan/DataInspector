# Release 构建 `windres: preprocessing failed.`

## 现象

在 VS Code 中构建 Release 目标，编译图标资源时失败：

```
D:\DevEnv\mingw-w64_810\mingw64\bin\windres.exe: preprocessing failed.
FAILED: CMakeFiles/DataInspector.dir/assets/DataInspector.rc.obj
```

Debug 构建正常；在终端里手敲同一条 windres 命令也能成功，只有 IDE 里的 Release 构建必挂。

## 根因：RC 编译器与 C++ 工具链不属于同一套

`CMakeLists.txt` 之前没有指定 `CMAKE_RC_COMPILER`，CMake 于是从 `PATH` 自动探测，两个构建目录探测到了不同结果：

| 构建 | C++ 编译器 | RC 编译器 |
| --- | --- | --- |
| Debug | LLVM-MinGW `clang++.exe` | `windres`（裸名） |
| Release | LLVM-MinGW `clang++.exe` | `D:/DevEnv/mingw-w64_810/mingw64/bin/windres.exe`（GNU binutils 2.30） |

GNU windres 自身不含预处理器，它通过 `popen` 调用同目录下的 `gcc -E -xc -DRC_INVOKED` 来预处理 `.rc`。实测：

- `PATH` 中含 `mingw-w64_810/bin`（有配套 gcc）时 → 成功；
- 把 `PATH` 剥到只剩 `System32` 后 → 立即 `preprocessing failed.`。

VS Code 的 `cmake.environment` 把 `Qt/6.8.3/llvm-mingw_64/bin` 前置，构建时的 `PATH` 与手动终端不同，因而 GNU windres 找不到配套 gcc。报错信息完全不提 gcc，极具误导性。

对照验证：LLVM 的 `llvm-windres.exe` 自带预处理，`PATH` 剥到只剩 `System32` 仍成功，产物包含 `.rsrc$01` / `.rsrc$02` 段。

## 修复

### `CMakePresets.json`

在 `qt6-clang-debug` 的 `cacheVariables` 中钉死 RC 编译器（`qt6-clang-release` 继承该预设）：

```json
"CMAKE_RC_COMPILER": "D:/Software/Qt/Tools/llvm-mingw1706_64/bin/llvm-windres.exe"
```

### `CMakeLists.txt`

增加防呆告警：C++ 编译器是 Clang 但 RC 编译器不属于 `llvm-windres` / `llvm-rc` 时给出 `message(WARNING ...)`，直接说明修法，避免再次被无信息量的报错困住。

## 验证

- Release 全新 configure 后缓存变为 `CMAKE_RC_COMPILER:...=.../llvm-windres.exe`。
- Release 全量构建 163/163 EXIT=0，RC 步骤变为 `Building RC object ...DataInspector.rc.res`。
- Release 产物提取图标的 md5 与 Debug 完全一致（`00d5ce7c1c8b554bba24c4d4256d13ff`）。
- 防呆告警在 Debug 旧缓存仍为裸 `windres` 时确实触发，刷新缓存后消失。

## 注意

错误的 RC 路径会固化在 `build_*/CMakeFiles/rules.ninja` 中，**只重新 build 不重新 configure 无效**。需删除 `CMakeCache.txt`（或整个构建目录）后按预设重新 configure。
