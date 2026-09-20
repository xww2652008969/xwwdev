#include "process.h"
#include "../utils/utils.h"

typedef NTSTATUS (NTAPI *PZwOpenThread)(
         PHANDLE ThreadHandle,
         ACCESS_MASK DesiredAccess,
         POBJECT_ATTRIBUTES ObjectAttributes,
         PCLIENT_ID ClientId
        );

/// 这里会被EAC 拦截
NTSTATUS OpenProcessHandle(XWW_REQUEST* request)
{
    if (!request) {
        return STATUS_INVALID_PARAMETER;
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,"打开进程\n"));

    if (request->BufferLength < sizeof(HANDLE)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,"缓冲区长度不足\n"));
        return STATUS_INVALID_PARAMETER;
    }
    CLIENT_ID cid;
    cid.UniqueProcess = (HANDLE)request->ProcessId;
    cid.UniqueThread = (HANDLE)0;
    ULONG attr = 0;
    HANDLE processHandle;
    OBJECT_ATTRIBUTES oa;
    attr |= OBJ_INHERIT;
    InitializeObjectAttributes(&oa, NULL, attr, NULL, NULL);
    NTSTATUS status = ZwOpenProcess(&processHandle, GENERIC_ALL, &oa, &cid);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,"ZwOpenProcess failed: 0x%X\n", status));
        return status;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,"获取句柄%d \n",processHandle));
    RtlCopyMemory(request->Buffer, &processHandle, sizeof(HANDLE));
    return status;
}

NTSTATUS OpenThreadHandle(XWW_REQUEST *request) {
    auto ZwOpenThread=(PZwOpenThread)GetNtRoutineAddress(L"ZwOpenThread");
    if (ZwOpenThread== nullptr){
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,"未能找到ZwOpenThread的函数\n"));
        return STATUS_INVALID_PARAMETER;
    }


    if (!request){
        return STATUS_INVALID_PARAMETER;
    }
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,"打开线程\n"));
    if (request->BufferLength < sizeof(HANDLE)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,"缓冲区长度不足\n"));
        return STATUS_INVALID_PARAMETER;
    }

    CLIENT_ID cid;
    OBJECT_ATTRIBUTES objectAttributes;
    cid.UniqueProcess = (HANDLE)request->ProcessId;
    // 优先取协议新增的 ThreadId；旧客户端只填 Handle 时回退到 Handle
    cid.UniqueThread  = (HANDLE)(request->ThreadId != 0 ? request->ThreadId : request->Handle);

    InitializeObjectAttributes(
            &objectAttributes,
            NULL,                       // 对象名必须为 NULL
            OBJ_INHERIT,                // 必须用普通用户态句柄：OBJ_KERNEL_HANDLE 建的是内核句柄，
                                        // 用户拿去 SuspendThread / GetThreadContext 会报 ERROR_INVALID_HANDLE
            NULL,
            NULL
    );

    HANDLE ThreadHandle = nullptr;

   auto status = ZwOpenThread(
           &ThreadHandle,
            THREAD_ALL_ACCESS,          // 请求的访问权限，按需调整
            &objectAttributes,
            &cid
    );

    if (!NT_SUCCESS(status)) {
        DbgPrint("ZwOpenThread failed with status: 0x%X\n", status);
        return status;
    }


    RtlCopyMemory(request->Buffer, &ThreadHandle, sizeof(HANDLE));

    return status;


}
