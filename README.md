# wtool

wtool 是一个 Windows 用户态 DLL（`wtool.dll`），配套内核驱动 `xww.sys`。
它把「加载驱动 → 内核态打开进程/线程 → 读写内存 → 手动映射注入 DLL」这条链路封装成一组 C 导出函数，可直接被 C/C++ 链接，也可以被 C# 通过 P/Invoke 调用。

- 用户态库：`wtool.dll`（本仓库）
- 内核驱动：`xww.sys`（配套仓库 `xww`，设备符号链接 `\\.\xww`）
- 语言：C++（x64），C# 调用示例见文末
- 平台：Windows x64

---

## 目录结构

| 文件 | 说明 |
| --- | --- |
| `Devices.h` / `Devices.cpp` | 与驱动通信的协议定义 + 驱动管理、进程/线程打开、内存读写的实现 |
| `injector.h` / `injector.cpp` | 手动映射注入（Manual Map）与远程调用的实现 |
| `dllmain.cpp` | DLL 入口 |
| `mapdll/` | 一份独立的 injector 副本（未参与主工程编译） |

---

## 快速开始

```cpp
#include "Devices.h"

// 1. 加载驱动（需管理员权限，xww.sys 需与 wtool.dll 同目录）
if (!LoadDriver()) {
    printf("加载驱动失败，错误码 = %lu\n", GetDriverLastError());
    return;
}

// 2. 打开设备
HANDLE dev = OpenDevices();

// 3. 内核态打开目标进程
HANDLE hProc = nullptr;
KernelOpenProcess(dev, pid, hProc);

// 4. 读写内存 / 注入 DLL ...

// 5. 收尾：先关句柄，再卸载驱动
CloseHandle(hProc);
CloseHandle(dev);
UnloadDriver();
```

> **卸载顺序很重要**：必须先用 `CloseHandle` 关闭设备句柄和驱动返回的所有句柄，再调用 `UnloadDriver`。
> 否则驱动无法真正停止，服务会残留为「待删除」状态（错误码 1072），需要手动清理注册表
> `HKLM\SYSTEM\CurrentControlSet\Services\xww` 或重启。

---

## API 参考

### 一、驱动管理

| 函数 | 说明 |
| --- | --- |
| `bool LoadDriver()` | 在 DLL 所在目录查找 `xww.sys`，以服务名 `xww` 注册并启动。服务已存在时先更新二进制路径；已在运行则视为成功。失败原因用 `GetDriverLastError()` 取。需管理员权限。 |
| `bool UnloadDriver()` | 停止并删除 `xww` 服务。服务本就不存在时返回 `true`。 |
| `DWORD GetDriverLastError()` | 返回最近一次 `LoadDriver` / `UnloadDriver` 的失败错误码，成功为 0。 |
| `HANDLE OpenDevices()` | 打开 `\\.\xww` 设备，失败返回 `nullptr`。 |

常见错误码：`5`(需管理员) / `2`(找不到 xww.sys) / `577`(驱动签名不被接受，需 `bcdedit /set testsigning on` 后重启) / `1072`(服务卡在待删除) / `1053`(DriverEntry 超时)。

### 二、进程 / 线程句柄

| 函数 | 说明 |
| --- | --- |
| `bool KernelOpenProcess(HANDLE hDevice, uint32_t pid, HANDLE& hProcess)` | 由驱动调用 `ZwOpenProcess`，以 `GENERIC_ALL` 打开目标进程，句柄通过 `hProcess` 返回。 |
| `bool KernelOpenThread(HANDLE hDevice, uint32_t pid, uint32_t tid, HANDLE& hThread)` | 由驱动调用 `ZwOpenThread`，以 `THREAD_ALL_ACCESS` 打开目标线程，句柄通过 `hThread` 返回。驱动返回的是普通（可继承）句柄，用户态可直接用于 `SuspendThread` / `GetThreadContext` / `SetThreadContext`。 |
| `bool GrantHandleAccess(HANDLE hDevice, uint64_t handle)` | 句柄提权：让驱动遍历句柄表，把指定句柄的访问权限提升为完全访问。进程句柄、线程句柄均可。`handle == 0` 时驱动直接返回失败。 |

### 三、内存读写

| 函数 | 说明 |
| --- | --- |
| `bool ReadAttach(HANDLE hDevice, uint32_t pid, uint64_t targetAddress, int dataLength, uint8_t* buffer)` | 从目标进程 `targetAddress` 读取 `dataLength` 字节到 `buffer`。 |
| `bool WriteAttach(HANDLE hDevice, uint32_t pid, uint64_t targetAddress, const uint8_t* buffer, int size)` | 把 `buffer` 中 `size` 字节写入目标进程 `targetAddress`。 |
| `bool AllocateRemoteMemory(HANDLE hDevice, uint32_t pid, uint32_t size, uint64_t& remoteBase)` | 在目标进程申请内存，基址通过 `remoteBase` 返回。 |
| `bool FreeRemoteMemory(HANDLE hDevice, uint32_t pid, uint64_t remoteBase)` | 释放由 `AllocateRemoteMemory` 申请的内存。 |

### 四、注入与远程调用

```cpp
// 手动映射注入：成功返回 DLL 在目标进程中的基址，失败返回 nullptr
extern "C" __declspec(dllexport)
HINSTANCE Mapdll(HANDLE hProc, BYTE* pSrcData, SIZE_T fileSize, DWORD dwProcessId);

// 远程调用：在目标进程 GUI 线程上劫持执行 add 指向的函数，成功返回 true
extern "C" __declspec(dllexport)
bool RemoteCall(HANDLE hProc, intptr_t add, DWORD pid);
```

参数说明：

| 参数 | 说明 |
| --- | --- |
| `hProc` | 由 `KernelOpenProcess` 得到的目标进程句柄 |
| `pSrcData` | DLL 文件的完整字节（从磁盘读入内存） |
| `fileSize` | DLL 文件字节数 |
| `dwProcessId` | 目标进程 PID，用于查找 GUI 线程 |
| `add` | `RemoteCall` 要在目标进程中执行的地址 |

`Mapdll` 内部按以下顺序工作：校验 PE → 在目标进程分配镜像内存 → 写入 PE 头与各节区 → 写入映射参数与 Shellcode → 劫持 GUI 线程执行 Shellcode（完成重定位、导入表、TLS、SEH 注册并调用 `DllMain`）→ 等待返回 → 可选地擦除 PE 头、清理无用节区、恢复节区保护属性。

C++ 直接链接时还可调用完整参数版本：

```cpp
HINSTANCE ManualMapDll(
    HANDLE hProc, BYTE* pSrcData, SIZE_T fileSize, DWORD dwProcessId,
    bool ClearHeader = true,            // 抹掉 PE 头
    bool ClearNonNeededSections = true, // 清空 .rsrc / .reloc 等无用节
    bool AdjustProtections = true,      // 按节区属性恢复内存保护
    bool SEHExceptionSupport = true,    // x64 下注册 SEH（需 /EHa 或 /EHc 编译）
    DWORD fdwReason = DLL_PROCESS_ATTACH,
    LPVOID lpReserved = 0);
```

> `ManualMapDll` 与 `RemoteCall` 会把诊断信息打印到标准输出，格式形如
> `[ManualMapDll] 失败：……` / `[ManualMapDll] 成功：DLL 已映射，pid=…，目标进程内基址=0x…`。
> 定义 `DISABLE_OUTPUT` 宏可关闭全部输出。

### 五、返回值约定

- 返回指针 / 句柄的函数：`nullptr` 表示失败。
- 返回 `bool` 的函数：`false` 表示失败，可用 `GetLastError()` 取错误码。
- `LoadDriver` / `UnloadDriver` 的失败原因用 `GetDriverLastError()` 取（不走 `GetLastError`）。

---

## C# 调用示例

仓库外提供了托管的 P/Invoke 封装（`Wtool` 静态类），失败时抛 `Win32Exception`：

```csharp
// 加载驱动并打开设备
Wtool.Load();
var dev = Wtool.Open();

// 内核态打开进程与线程
var hProc = Wtool.OpenProcess(dev, pid);
var tid   = 1234;                       // 目标线程 TID
var hThrd = Wtool.OpenThread(dev, pid, tid);
Wtool.Grant(dev, hThrd);                // 句柄提权

// 读写内存
byte[] head = Wtool.Read(dev, pid, baseAddress, 2);
Wtool.Write(dev, pid, baseAddress, new byte[] { 1, 2, 3, 4 });
int hp = Wtool.ReadValue<int>(dev, pid, someAddress);
Wtool.WriteValue(dev, pid, someAddress, 999);

// 申请 / 释放远程内存
ulong remote = Wtool.Alloc(dev, pid, 0x1000);
Wtool.Free(dev, pid, remote);

// 手动映射注入，返回目标进程内基址
var dllBytes = File.ReadAllBytes(@"C:\path\to\your.dll");
IntPtr hMod = Wtool.MapDll(hProc, dllBytes, pid);
Console.WriteLine($"注入成功，基址 = 0x{hMod.ToInt64():X}");

// 收尾
Wtool.Close(hProc);
Wtool.Close(dev);
Wtool.Unload();
```

对应的原生声明（可用于其它语言自行封装）：

```csharp
[LibraryImport("wtool.dll", SetLastError = true)]
[return: MarshalAs(UnmanagedType.I1)]
internal static partial bool KernelOpenThread(IntPtr hDevice, uint pid, uint tid, out IntPtr hThread);
```

> 原生 `bool` 是 1 字节（C++ `bool`），P/Invoke 声明必须加 `[return: MarshalAs(UnmanagedType.I1)]`，
> 否则会按 4 字节 Win32 `BOOL` 解析。使用源生成的 `LibraryImport` 时，返回值为指针的函数
> **不能**加这个特性（会触发 `SYSLIB1052`）。

---

## 通信协议（供自行实现客户端参考）

- 设备路径：`\\.\xww`
- IOCTL：`CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)`，值恒为 `0x222000`
- 传输方式：`METHOD_BUFFERED`，请求与响应共用同一缓冲区，头部固定 36 字节，其后是变长数据区

操作码：

| 操作码 | 名称 | 说明 |
| --- | --- | --- |
| `0x601` | `XWW_OP_GRANT_HANDLE_ACCESS` | 句柄提权 |
| `0x701` | `XWW_OP_OPEN_PROCESS_HANDLE` | 打开进程句柄 |
| `0x702` | `XWW_OP_OPEN_THREAD_HANDLE` | 打开线程句柄 |
| `0x801` | `XWW_OP_READ_MEMORY` | 读内存 |
| `0x802` | `XWW_OP_WRITE_MEMORY` | 写内存 |
| `0x803` | `XWW_OP_ALLOCATE_MEMORY` | 申请内存 |
| `0x804` | `XWW_OP_FREE_MEMORY` | 释放内存 |

请求结构（`pack(1)`，头部 36 字节）：

| 偏移 | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 0 | `Operation` | `uint32` | 操作码 |
| 4 | `ProcessId` | `uint32` | 目标 PID |
| 8 | `ThreadId` | `uint32` | 目标 TID（`0x702` 使用） |
| 12 | `TargetAddress` | `uint64` | 目标虚拟地址（读 / 写 / 释放） |
| 20 | `BufferLength` | `uint32` | 变长区有效长度 |
| 24 | `AllocateSize` | `uint32` | 申请内存大小（`0x803`） |
| 28 | `Handle` | `uint64` | 输入：待提权句柄 / 待打开的 TID；输出：进程句柄 / 分配基址 |
| 36 | `Buffer[]` | 变长 | 读写数据；`0x701` / `0x702` 用它回传句柄 |

> 协议改动提示：`ThreadId` 是后加入的字段，插入后所有后续字段偏移 +4、头部由 32 字节变为 36 字节。
> 自行实现客户端时务必与 `Devices.h` 中的 `static_assert` 保持一致，否则所有操作都会静默错位。
> 驱动对 `0x702` 兼容两种填法：优先读 `ThreadId`，为 0 时回退读 `Handle`。

---

## 注意事项

1. **需要管理员权限**，且驱动必须已签名或系统开启测试签名（`bcdedit /set testsigning on` 后重启）。
2. **x64 工程**，注入目标也必须是 x64；`Mapdll` 会校验 PE 机器码，不匹配直接失败。
3. SEH 异常支持需要以 `/EHa` 或 `/EHc` 编译被注入的 DLL。
4. 卸载驱动前必须关闭所有句柄，否则服务会卡在待删除状态（详见「快速开始」）。
5. 日志中的中文按 UTF-8 输出，默认 GBK 控制台可能显示乱码：入口处调用 `SetConsoleOutputCP(CP_UTF8)`，或运行前执行 `chcp 65001`，并给工程加 `/utf-8` 编译选项。
6. 开源发布时建议忽略构建产物：`obj/`、`x64/`、`*.user`、`.vs/`、`out/`。

---

## 免责声明

本项目涉及内核驱动与进程注入技术，**仅用于安全研究、逆向学习与拥有明确授权的系统（如自有环境、渗透测试授权目标）**。
请勿将其用于未授权的系统、软件破解、游戏作弊或任何违反当地法律法规的行为。作者不对任何滥用行为负责。
