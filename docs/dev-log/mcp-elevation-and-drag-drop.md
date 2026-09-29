# 拖拽启动后无法再拖入文件：UAC 提权与清单

## 现象

用 Enigma Virtual Box 打包成单文件后，把数据文件拖到 exe 上启动会弹出"以管理员身份运行"提示；程序启动并成功加载了该文件，但之后**再也无法把文件拖进窗口**加载。直接双击启动的程序则一切正常。

## 根因：UIPI 阻止低完整性级别向高完整性级别拖放

与图标、`openStartupFiles()` 或 QML `DropArea` 都无关。已用 `tests/startupdrop_test.cpp` 证明：先用命令行参数导入文件，再向窗口投递真实的 `QDragEnterEvent` / `QDropEvent`，文件照常加载 —— 应用层逻辑没有问题。

真正的原因是 Windows 的 **User Interface Privilege Isolation (UIPI)**：资源管理器以普通完整性级别运行，提权后的进程处于高完整性级别，系统禁止前者向后者拖放。被拒绝发生在 OLE 层，应用收不到任何事件，因此表现为"窗口完全不响应拖拽"且没有任何报错。

为什么会被提权：构建出的 `DataInspector.exe` **没有嵌入应用程序清单**（实测该二进制中搜不到 `requestedExecutionLevel`）。对于无清单的可执行文件，Windows 会启用 **installer detection** 启发式：文件名或内容包含 `setup`、`install`、`update`、`patch` 等特征，或由打包器生成的自解压型加载器，都可能被判定为安装程序而要求提权。Enigma Virtual Box 生成的单文件加载器正好落入这一类，于是弹出 UAC，进程被拉到高完整性级别，拖放随即失效。

"拖拽启动"只是让问题显现得更早：那次启动本身就带 UAC 提示，之后的拖入自然全部被拒。

## 修复

### `assets/DataInspector.manifest`（新增）

显式声明 `asInvoker`，让进程始终继承调用者的完整性级别，不再被 installer detection 误判：

```xml
<requestedExecutionLevel level="asInvoker" uiAccess="false"/>
```

同时顺带声明了 per-monitor v2 DPI 感知、UTF-8 活动代码页，以及 Windows 8.1/10/11 的 `supportedOS`（缺少 `supportedOS` 时系统会按兼容模式回报较低的版本号）。

### `assets/DataInspector.rc`

以标准资源 ID 嵌入清单：

```
#include <windows.h>
IDI_ICON1 ICON "icon/DataInspector.ico"
1 RT_MANIFEST "DataInspector.manifest"
```

`1` 即 `CREATEPROCESS_MANIFEST_RESOURCE_ID`，是 Windows 加载 exe 时读取的清单资源号。

### `CMakeLists.txt`

为 `.rc` 声明 `OBJECT_DEPENDS`，清单或图标改动后能正确触发重新编译资源。

### 运行期提示（`startupfiles.{h,cpp}`、`appcontroller_loading.cpp`）

新增 `runningElevated()`，用 `OpenProcessToken` + `TokenElevation` 判断当前是否提权。`openStartupFiles()` 在提权时于状态栏给出明确提示，而不是让窗口"静默失灵"：

> 以管理员身份运行，无法从资源管理器拖放文件到窗口；请以普通用户身份启动

这层提示是兜底 —— 用户仍可能主动右键"以管理员身份运行"，此时 UIPI 限制依旧存在，属于 Windows 的设计，应用无法绕过。

## 验证

- 修复前：在构建产物中搜索 `requestedExecutionLevel` → `NO_MANIFEST`。
- 修复后：`MANIFEST_FOUND` + `ASINVOKER_FOUND`；通过 `FindResource(hMod, 1, RT_MANIFEST)` 确认资源真实存在，大小 1647 字节。
- 图标未受影响：从 exe 提取图标的 md5 仍为 `00d5ce7c1c8b554bba24c4d4256d13ff`，与加清单前一致。
- Release 全量构建 EXIT=0 无告警；Debug `ctest` **10/10 通过**，含新增的 `startupdrop_test`。

## 重新打包须知（Enigma Virtual Box）

清单嵌在原始 exe 里，**打包后仍需确认最终单文件带有 `asInvoker` 清单**：部分打包器会生成自己的加载器外壳而不继承原 exe 的清单。若打包后仍弹 UAC：

1. 在 Enigma Virtual Box 中确认未勾选任何"以管理员身份运行 / 请求提权"选项。
2. 检查输出文件名不含 `setup`、`install`、`update`、`patch` 等触发 installer detection 的词。
3. 必要时对打包后的单文件再嵌一次清单：
   `mt.exe -manifest DataInspector.manifest -outputresource:Packed.exe;#1`
4. 验证：搜索二进制中的 `asInvoker`，或右键属性确认快捷方式未勾选"以管理员身份运行"。

## 未做的部分

- 未实现提权状态下的拖放替代通道（需要 `ChangeWindowMessageFilterEx` 放行 `WM_DROPFILES` 并改用 `DragAcceptFiles`，且仅对 `WM_DROPFILES` 有效、对 OLE `IDropTarget` 无效；Qt 使用后者，无法直接套用）。
- 未提供打包脚本；上述 Enigma Virtual Box 步骤为手工流程。
