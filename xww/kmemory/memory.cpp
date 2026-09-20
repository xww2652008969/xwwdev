#include "memory.h"

#include "../utils/utils.h"

// 安全读取虚拟内存：通过 MmCopyMemory 读取，避免直接解引用导致蓝屏
static BOOLEAN SafeReadVirtualMemory(PVOID address, PVOID buffer, ULONG length) {
    BOOLEAN success = FALSE;
    PVOID temp = ExAllocatePool(NonPagedPool, length);
    if (temp) {
        SIZE_T bytesCopied;
        MM_COPY_ADDRESS copyAddress;
        copyAddress.VirtualAddress = address;
        NTSTATUS status = MmCopyMemory(temp, copyAddress, length, MM_COPY_MEMORY_VIRTUAL, &bytesCopied);
        if (NT_SUCCESS(status)) {
            RtlCopyMemory(buffer, temp, bytesCopied);
            success = TRUE;
        }
        ExFreePool(temp);
    }
    return success;
}

// 安全写虚拟内存：仅允许写用户态地址，内核地址直接拒绝（CR0.WP 改写风险高，暂不支持）
static BOOLEAN SafeWriteVirtualMemory(PVOID address, PVOID buffer, ULONG length)
{
    if (address > MM_HIGHEST_USER_ADDRESS) {
        return FALSE;
    }

    BOOLEAN success = FALSE;
    __try {
        RtlCopyMemory(address, buffer, length);
        success = TRUE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return success;
}

NTSTATUS WriteProcessMemory(XWW_REQUEST* request) {
    BOOLEAN attached = FALSE;
    PEPROCESS process = NULL;
    KAPC_STATE apcState;
    ULONG pid = request->ProcessId;

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "写入数据: pid=%lu, TargetAddress=%llu, BufferLength=%d\n",
        request->ProcessId, request->TargetAddress, request->BufferLength));

    if (pid != 4 && pid != 0 && pid != static_cast<ULONG>(reinterpret_cast<ULONG_PTR>(PsGetCurrentProcessId()))) {
        if (NT_SUCCESS(PsLookupProcessByProcessId(reinterpret_cast<HANDLE>(pid), &process))) {
            KeStackAttachProcess(process, &apcState);
            attached = TRUE;
        }
    }

    BOOLEAN success = SafeWriteVirtualMemory((PVOID)request->TargetAddress, request->Buffer, request->BufferLength);

    if (attached) {
        KeUnstackDetachProcess(&apcState);
        ObDereferenceObject(process);
    }

    return success ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

NTSTATUS ReadProcessMemory(XWW_REQUEST* request) {
    PVOID temp = ExAllocatePool(NonPagedPool, request->BufferLength);
    if (!temp) return STATUS_MEMORY_NOT_ALLOCATED;

    BOOLEAN attached = FALSE;
    PEPROCESS process = NULL;
    KAPC_STATE apcState;
    ULONG pid = request->ProcessId;

    if (pid != 4 && pid != 0 && pid != (ULONG)(ULONG_PTR)PsGetCurrentProcessId()) {
        if (NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)pid, &process))) {
            KeStackAttachProcess(process, &apcState);
            attached = TRUE;
        }
    }

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "读取数据: pid=%lu, TargetAddress=%p, attached=%d\n",
        request->ProcessId, (PVOID)request->TargetAddress, attached));

    BOOLEAN success = SafeReadVirtualMemory((PVOID)request->TargetAddress, temp, request->BufferLength);

    if (attached) {
        KeUnstackDetachProcess(&apcState);
        ObDereferenceObject(process);
    }

    if (!success) {
        ExFreePool(temp);
        return STATUS_UNSUCCESSFUL;
    }

    RtlCopyMemory(request->Buffer, temp, request->BufferLength);
    ExFreePool(temp);
    return STATUS_SUCCESS;
}

NTSTATUS AllocateProcessMemory(XWW_REQUEST* request) {
    PEPROCESS targetProcess = NULL;
    PVOID baseAddress = NULL;
    SIZE_T regionSize = request->AllocateSize;
    HANDLE processHandle = NULL;

    NTSTATUS status = PsLookupProcessByProcessId(reinterpret_cast<HANDLE>(request->ProcessId), &targetProcess);
    if (!NT_SUCCESS(status)) {
        DbgPrint("获取进程失败: 0x%X\n", status);
        return status;
    }

    status = ObOpenObjectByPointer(targetProcess, OBJ_KERNEL_HANDLE, NULL,
        PROCESS_ALL_ACCESS, *PsProcessType, KernelMode, &processHandle);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(targetProcess);
        DbgPrint("打开进程失败: 0x%X\n", status);
        return status;
    }

    status = ZwAllocateVirtualMemory(processHandle, &baseAddress, 0, &regionSize,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    ObDereferenceObject(targetProcess);
    ZwClose(processHandle);

    if (NT_SUCCESS(status)) {
        // request 指向系统缓冲区，直接写回分配到的基址
        request->Handle = (ULONG64)(ULONG_PTR)baseAddress;
    } else {
        DbgPrint("分配内存失败: 0x%X\n", status);
    }

    return status;
}

NTSTATUS FreeProcessMemory(XWW_REQUEST* request) {
    NTSTATUS status = STATUS_SUCCESS;
    PEPROCESS targetProcess = NULL;
    KAPC_STATE apcState;
    PVOID baseAddress = reinterpret_cast<PVOID>(request->TargetAddress);
    SIZE_T regionSize = 0; // 传 0 表示释放整个区域
    HANDLE pid = reinterpret_cast<HANDLE>(request->ProcessId);

    // 查找目标进程
    status = PsLookupProcessByProcessId(pid, &targetProcess);
    if (!NT_SUCCESS(status)) {
        DbgPrint("PsLookupProcessByProcessId failed: 0x%X\n", status);
        return status;
    }

    // 附加到目标进程
    KeStackAttachProcess(targetProcess, &apcState);
    __try {
        // 直接用 NtCurrentProcess()，不需要打开句柄
        status = ZwFreeVirtualMemory(
            NtCurrentProcess(),
            &baseAddress,
            &regionSize,
            MEM_RELEASE
        );
    } __finally {
        KeUnstackDetachProcess(&apcState);
        ObDereferenceObject(targetProcess);
    }

    if (NT_SUCCESS(status)) {
        DbgPrint("Freed memory at %p in process %lu\n", baseAddress, (ULONG)(ULONG_PTR)pid);
    } else {
        DbgPrint("ZwFreeVirtualMemory failed: 0x%X\n", status);
    }

    return status;
}

// ==================== 物理内存读写（尚未接入 IOCTL 分发） ====================

static NTSTATUS ReadPhysicalMemory(PVOID physicalAddress, PVOID buffer, SIZE_T size, SIZE_T* bytesTransferred)
{
    MM_COPY_ADDRESS copyAddress = { 0 };
    copyAddress.PhysicalAddress.QuadPart = (LONG64)physicalAddress;
    return MmCopyMemory(buffer, copyAddress, size, MM_COPY_MEMORY_PHYSICAL, bytesTransferred);
}

static ULONG_PTR GetProcessCr3(UINT32 processId)
{
    PEPROCESS process{ 0 };
    NTSTATUS status = PsLookupProcessByProcessId(reinterpret_cast<HANDLE>(processId), &process);
    if (!NT_SUCCESS(status)) {
        DbgPrint("PsLookupProcessByProcessId failed: 0x%X\n", status);
        return 0;
    }
    ULONG_PTR cr3 = *reinterpret_cast<ULONG_PTR*>(reinterpret_cast<PUCHAR>(process) + 0x28);
    ObDereferenceObject(process);
    return cr3;
}

// 通过 CR3 遍历四级页表，将虚拟地址翻译为物理地址
static ULONG64 TranslateVirtualToPhysical(ULONG64 cr3, ULONG64 virtualAddress)
{
    cr3 &= ~0xf;
    // 获取页面偏移量
    ULONG64 pageOffset = virtualAddress & ~(~0ul << 12);
    SIZE_T bytesTransferred = 0;
    ULONG64 pml4e = 0, pdpte = 0, pde = 0, pte = 0;

    // 读取虚拟地址所在的 PML4E（四级页表项）
    ReadPhysicalMemory((PVOID)(cr3 + 8 * ((virtualAddress >> 39) & 0x1ff)), &pml4e, sizeof(pml4e), &bytesTransferred);
    // P（存在位）为 0，表示该页表项没有映射物理内存
    if (~pml4e & 1) {
        return 0;
    }

    // 读取虚拟地址所在的 PDPTE（三级页表项）
    ReadPhysicalMemory((PVOID)((pml4e & ((~0xfull << 8) & 0xfffffffffull)) + 8 * ((virtualAddress >> 30) & 0x1ff)), &pdpte, sizeof(pdpte), &bytesTransferred);
    if (~pdpte & 1) {
        return 0;
    }
    // PS（页面大小）为 1，表示映射的是 1GB 大页，直接计算物理地址
    if (pdpte & 0x80) {
        return (pdpte & (~0ull << 42 >> 12)) + (virtualAddress & ~(~0ull << 30));
    }

    // 读取虚拟地址所在的 PDE（二级页表项）
    ReadPhysicalMemory((PVOID)((pdpte & ((~0xfull << 8) & 0xfffffffffull)) + 8 * ((virtualAddress >> 21) & 0x1ff)), &pde, sizeof(pde), &bytesTransferred);
    if (~pde & 1) {
        return 0;
    }
    // PS 为 1，表示映射的是 2MB 大页，直接计算物理地址
    if (pde & 0x80) {
        return (pde & ((~0xfull << 8) & 0xfffffffffull)) + (virtualAddress & ~(~0ull << 21));
    }

    // 读取虚拟地址所在的 PTE（一级页表项），计算出物理地址
    ReadPhysicalMemory((PVOID)((pde & ((~0xfull << 8) & 0xfffffffffull)) + 8 * ((virtualAddress >> 12) & 0x1ff)), &pte, sizeof(pte), &bytesTransferred);
    pte &= ~0xfull << 8 & 0xfffffffffull;
    if (!pte) {
        return 0;
    }
    return pte + pageOffset;
}

// 按 CR3 走页表读取目标进程物理内存（未接入分发，供后续使用）
NTSTATUS ReadPhysicalMemoryData(XWW_REQUEST* request) {
    ULONG_PTR cr3 = GetProcessCr3(request->ProcessId);
    if (cr3 == 0) {
        DbgPrint("GetProcessCr3 failed\n");
        return STATUS_UNSUCCESSFUL;
    }
    DbgPrint("cr3 %p\n", (PVOID)cr3);

    ULONG64 physicalAddress = TranslateVirtualToPhysical(cr3, request->TargetAddress);
    if (physicalAddress == 0) {
        DbgPrint("TranslateVirtualToPhysical failed\n");
        return STATUS_UNSUCCESSFUL;
    }

    PVOID temp = ExAllocatePool(NonPagedPool, request->BufferLength);
    if (!temp) return STATUS_MEMORY_NOT_ALLOCATED;

    SIZE_T bytesCopied = 0;
    MM_COPY_ADDRESS copyAddress;
    copyAddress.PhysicalAddress.QuadPart = (LONG64)physicalAddress;
    NTSTATUS status = MmCopyMemory(temp, copyAddress, request->BufferLength, MM_COPY_MEMORY_PHYSICAL, &bytesCopied);
    if (!NT_SUCCESS(status)) {
        DbgPrint("MmCopyMemory failed: 0x%X\n", status);
        ExFreePool(temp);
        return status;
    }

    RtlCopyMemory(request->Buffer, temp, request->BufferLength);
    ExFreePool(temp);
    return STATUS_SUCCESS;
}
