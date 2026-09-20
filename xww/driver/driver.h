#pragma once
#include <ntifs.h>

// ============================================================
// 通信协议定义（内核态 / 用户态共用）
// 注意：字段顺序与打包方式即线上协议，改动需同步用户态客户端
// ============================================================

// 操作码（数值保持不变，与现有用户态客户端兼容）
typedef enum _XWW_OPERATION : UINT32 {
    XWW_OP_GRANT_HANDLE_ACCESS = 0x601,  // 句柄提权（授予完全访问权限）
    XWW_OP_OPEN_PROCESS_HANDLE = 0x701,  // 打开目标进程句柄
    XWW_OP_READ_MEMORY         = 0x801,  // 读取目标进程内存
    XWW_OP_WRITE_MEMORY        = 0x802,  // 写入目标进程内存
    XWW_OP_ALLOCATE_MEMORY     = 0x803,  // 在目标进程申请内存
    XWW_OP_FREE_MEMORY         = 0x804,  // 释放目标进程内存
    Xww_OP_OPEN_THREAD_HANDLE  =0x702,
} XWW_OPERATION;

#pragma pack(1)
// 请求 / 响应共用结构体
typedef struct _XWW_REQUEST {
    UINT32   Operation;       // 操作码，取值见 XWW_OPERATION
    UINT32   ProcessId;       // 目标进程 PID
    UINT32   ThreadId;       // 目标线程 PID
    UINT64   TargetAddress;   // 目标虚拟地址（读 / 写 / 释放内存时使用）
    UINT32   BufferLength;    // Buffer 中有效数据长度（字节）
    UINT32   AllocateSize;    // 申请内存大小（仅 XWW_OP_ALLOCATE_MEMORY）
    UINT64   Handle;          // 输入：待提权的句柄值；输出：进程句柄 / 分配到的基址
    UCHAR    Buffer[1];       // 变长数据区（读写数据 / DLL 镜像）
} XWW_REQUEST, *PXWW_REQUEST;
#pragma pack()

// 固定头部长度（不含变长 Buffer），用于接收请求时的最小长度校验
#define XWW_REQUEST_HEADER_SIZE ((ULONG)offsetof(XWW_REQUEST, Buffer))
