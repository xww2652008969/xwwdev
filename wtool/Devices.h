#pragma once
#include <cstdint>
#include <cstddef>
#include <Windows.h>
#include <winioctl.h>
#include <vector>

// ============================================================
// 与 xww 驱动（v3 分支 driver/driver.h）共用的通信协议定义
// 字段顺序、宽度与打包方式即线上协议，驱动侧改动必须同步此处
// ============================================================

// 驱动创建的符号链接为 \DosDevices\xww，用户态通过该路径打开
#define XWW_DEVICE_PATH L"\\\\.\\xww"

// IOCTL 控制码：CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
// 数值恒为 0x222000。必须为 METHOD_BUFFERED，驱动使用 Irp->AssociatedIrp.SystemBuffer 取请求
#define IOCTL_XWW_REQUEST CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

// 操作码：数值与驱动 XWW_OPERATION 枚举一一对应，不可变更
typedef enum _XWW_OPERATION : uint32_t {
	XWW_OP_GRANT_HANDLE_ACCESS = 0x601,  // 句柄提权（授予完全访问权限）
	XWW_OP_OPEN_PROCESS_HANDLE = 0x701,  // 打开目标进程句柄
	XWW_OP_OPEN_THREAD_HANDLE  = 0x702,  // 打开目标线程句柄（驱动侧 OpenThThreadHandle）
	XWW_OP_READ_MEMORY         = 0x801,  // 读取目标进程内存
	XWW_OP_WRITE_MEMORY        = 0x802,  // 写入目标进程内存
	XWW_OP_ALLOCATE_MEMORY     = 0x803,  // 在目标进程申请内存
	XWW_OP_FREE_MEMORY         = 0x804,  // 释放目标进程内存
} XWW_OPERATION;

#pragma pack(push, 1)
// 请求 / 响应共用结构体，布局与驱动端 XWW_REQUEST 严格一致
typedef struct _XWW_REQUEST {
	uint32_t Operation;       // 操作码，取值见 XWW_OPERATION
	uint32_t ProcessId;       // 目标进程 PID
	uint32_t ThreadId;        // 目标线程 TID（0x702 使用；驱动当前版本暂未读取该字段）
	uint64_t TargetAddress;   // 目标虚拟地址（读 / 写 / 释放内存时使用）
	uint32_t BufferLength;    // Buffer 中有效数据长度（字节）
	uint32_t AllocateSize;    // 申请内存大小（仅 XWW_OP_ALLOCATE_MEMORY）
	uint64_t Handle;          // 输入：待提权的句柄值 / 待打开的 TID；输出：进程句柄 / 分配到的基址
	uint8_t  Buffer[1];       // 变长数据区起始（读写数据 / DLL 镜像）
} XWW_REQUEST, *PXWW_REQUEST;
#pragma pack(pop)

// 固定头部长度（36 字节，不含变长数据区）。
// 驱动以该值做请求最小长度校验，实际发送缓冲区长度必须 >= 此值。
// 注意：因末尾含 Buffer[1]，sizeof(XWW_REQUEST) 为 37，不可用于计算数据区偏移。
#define XWW_REQUEST_HEADER_SIZE ((size_t)offsetof(XWW_REQUEST, Buffer))

// ------------------------------------------------------------
// 协议布局断言：与驱动端 XWW_REQUEST 逐字段对齐。
// 若驱动改动结构体布局，此处会在编译期报错，避免运行期静默错位。
// ------------------------------------------------------------
static_assert(IOCTL_XWW_REQUEST == 0x222000, "IOCTL 控制码必须与驱动一致");
static_assert(sizeof(uint32_t) == 4 && sizeof(uint64_t) == 8, "基础类型宽度异常");
static_assert(offsetof(XWW_REQUEST, Operation)     == 0,  "Operation 偏移应为 0");
static_assert(offsetof(XWW_REQUEST, ProcessId)     == 4,  "ProcessId 偏移应为 4");
static_assert(offsetof(XWW_REQUEST, ThreadId)      == 8,  "ThreadId 偏移应为 8");
static_assert(offsetof(XWW_REQUEST, TargetAddress) == 12, "TargetAddress 偏移应为 12");
static_assert(offsetof(XWW_REQUEST, BufferLength)  == 20, "BufferLength 偏移应为 20");
static_assert(offsetof(XWW_REQUEST, AllocateSize)  == 24, "AllocateSize 偏移应为 24");
static_assert(offsetof(XWW_REQUEST, Handle)        == 28, "Handle 偏移应为 28");
static_assert(offsetof(XWW_REQUEST, Buffer)        == 36, "Buffer 偏移应为 36");
static_assert(XWW_REQUEST_HEADER_SIZE == 36, "请求头部长度必须为 36 字节");

#ifdef __cplusplus
extern "C" {
#endif

	// 向目标进程写入内存（XWW_OP_WRITE_MEMORY）
	__declspec(dllexport) bool WriteAttach(HANDLE hDevice, uint32_t pid, uint64_t targetAddress, const uint8_t* buffer, int size);

	// 从目标进程读取内存（XWW_OP_READ_MEMORY），结果写入 buffer
	__declspec(dllexport) bool ReadAttach(HANDLE hDevice, uint32_t pid, uint64_t targetAddress, int dataLength, uint8_t* buffer);

	// 内核态打开目标进程，返回进程句柄到 hProcess
	__declspec(dllexport) bool KernelOpenProcess(HANDLE hDevice, uint32_t pid, HANDLE& hProcess);

	// 内核态打开目标线程（XWW_OP_OPEN_THREAD_HANDLE），返回线程句柄到 hThread
	__declspec(dllexport) bool KernelOpenThread(HANDLE hDevice, uint32_t pid, uint32_t tid, HANDLE& hThread);

	// 句柄提权：将 handle（进程 / 线程句柄）提升为完全访问权限
	__declspec(dllexport) bool GrantHandleAccess(HANDLE hDevice, uint64_t handle);

	// 在目标进程申请内存（XWW_OP_ALLOCATE_MEMORY），基址通过 hRemoteBase 返回
	__declspec(dllexport) bool AllocateRemoteMemory(HANDLE hDevice, uint32_t pid, uint32_t size, uint64_t& remoteBase);

	// 释放目标进程中已申请的内存（XWW_OP_FREE_MEMORY）
	__declspec(dllexport) bool FreeRemoteMemory(HANDLE hDevice, uint32_t pid, uint64_t remoteBase);

	// 打开驱动设备，失败返回 nullptr
	__declspec(dllexport) HANDLE OpenDevices();

	// 加载驱动：在本 DLL 所在目录查找 xww.sys，以服务名 "xww" 注册并启动。
	// 不做签名校验；服务已存在时先更新其二进制路径再启动；已在运行则视为成功。
	__declspec(dllexport) bool LoadDriver();

	// 卸载驱动：停止 "xww" 服务并删除该服务
	__declspec(dllexport) bool UnloadDriver();

	// 返回 LoadDriver / UnloadDriver 最近一次失败的错误码（成功时为 0）
	__declspec(dllexport) DWORD GetDriverLastError();

#ifdef __cplusplus
}
#endif
