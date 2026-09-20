#include "driver/driver.h"
#include "kmemory/memory.h"
#include "process/process.h"
#include <ntifs.h>
#include <ntddk.h>

#include "handle/handle.h"
#include "utils/utils.h"

// 设备创建相关常量
#define DEVICE_NAME L"\\Device\\xww"
#define SYMLINK_NAME L"\\DosDevices\\xww"


// 函数前向声明
NTSTATUS DispatchCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
VOID DriverUnload(PDRIVER_OBJECT DriverObject);




// 驱动入口函数
 extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    // 调试输出，表示驱动已初始化
    DbgPrint("Driver initialized.\n");
    UNREFERENCED_PARAMETER(RegistryPath);

    PDEVICE_OBJECT deviceObject = nullptr;
    UNICODE_STRING deviceName;
    UNICODE_STRING symlinkName;
    // 设置驱动卸载函数
    DriverObject->DriverUnload = DriverUnload;

    // 初始化设备名称
    RtlInitUnicodeString(&deviceName, DEVICE_NAME);
    RtlInitUnicodeString(&symlinkName, SYMLINK_NAME);

    // 创建设备对象
    NTSTATUS status = IoCreateDevice(
        DriverObject,
        0,
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &deviceObject
    );

    if (!NT_SUCCESS(status)) {
        return status;
    }



    status = IoCreateSymbolicLink(&symlinkName, &deviceName);

    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(deviceObject);
        return status;
    }


    DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;

     KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,"加载驱动\n"));

    auto address = GetNtRoutineAddress(L"NtCreateThreadEx");
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,"地址 %p\n ", address));


    return STATUS_SUCCESS;
}

// 驱动卸载函数
VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING symlinkName;


    RtlInitUnicodeString(&symlinkName, SYMLINK_NAME);

    // 删除符号链接
    IoDeleteSymbolicLink(&symlinkName);

    // 删除设备对象
    if (DriverObject->DeviceObject != nullptr) {
        IoDeleteDevice(DriverObject->DeviceObject);
    }

}

// 创建/关闭 IRP 处理函数
NTSTATUS DispatchCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);


    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}
// IOCTL 设备控制 IRP 处理函数
NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);
    ULONG inputLength = irpStack->Parameters.DeviceIoControl.InputBufferLength;
    NTSTATUS status = STATUS_SUCCESS;

    // 最小长度校验：缓冲区必须能容纳固定头部，防止越界读取
    if (inputLength < XWW_REQUEST_HEADER_SIZE) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "请求长度过小: %lu, 至少需要 %lu\n", inputLength, XWW_REQUEST_HEADER_SIZE));
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }

    auto* request = static_cast<XWW_REQUEST*>(Irp->AssociatedIrp.SystemBuffer);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "收到请求 op=0x%X, pid=%lu, TargetAddress=%llu\n",
        request->Operation, request->ProcessId, request->TargetAddress));

    switch (request->Operation) {
        case XWW_OP_READ_MEMORY:
            status = ReadProcessMemory(request);
            break;
        case XWW_OP_WRITE_MEMORY:
            status = WriteProcessMemory(request);
            break;
        case XWW_OP_ALLOCATE_MEMORY:
            status = AllocateProcessMemory(request);
            break;
        case XWW_OP_FREE_MEMORY:
            status = FreeProcessMemory(request);
            break;
        case XWW_OP_OPEN_PROCESS_HANDLE:
            status = OpenProcessHandle(request);   //获取目标进程句柄
            break;
        case XWW_OP_GRANT_HANDLE_ACCESS:
            //句柄提权
            GrantHandleAccess(request);
            break;
        case Xww_OP_OPEN_THREAD_HANDLE:
            status = OpenThreadHandle(request);   // 获取目标线程句柄
            break;
        default:
            KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                "未知操作码: 0x%X\n", request->Operation));
            status = STATUS_INVALID_PARAMETER;
            break;
    }
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = inputLength;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}
