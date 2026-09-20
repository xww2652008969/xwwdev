#include "handle.h"
#include <ntifs.h>
#include <ntimage.h>
#include <ntstrsafe.h>
#include "../utils/utils.h"
#include <wdm.h>

//0x80 bytes (sizeof)
typedef struct HANDLE_TABLE
{
    ULONG NextHandleNeedingPool;                                            //0x0
    LONG ExtraInfoPages;                                                      //0x4
    volatile ULONGLONG TableCode;                                           //0x8
} HANDLE_TABLE, *PHANDLE_TABLE;

// 简化版，仅保留常用字段
typedef struct HANDLE_TABLE_ENTRY {
    union {
        PVOID Object;              // 指向对象
        ULONG_PTR Value;           // 原始值
    };
    ULONG GrantedAccessBits;       // 访问权限位
    USHORT Attributes;             // 句柄属性 (可继承等)
    USHORT RefCount;               // 引用计数
} HANDLE_TABLE_ENTRY, *PHANDLE_TABLE_ENTRY;


// 遍历当前进程的句柄表，将指定句柄的访问权限提升为 PROCESS_ALL_ACCESS
static void ElevateHandleAccess(ULONG64 handleValue) {
    PEPROCESS process = nullptr;
    NTSTATUS status = PsLookupProcessByProcessId(PsGetCurrentProcessId(), &process);
    if (!NT_SUCCESS(status) || !process) {
        KdPrint(("Failed to get current process\n"));
        return;
    }

    __try {
        KdPrint(("Current process EPROCESS address: %p\n", process));

        // Get handle table with proper offset validation
        auto pTable = *reinterpret_cast<HANDLE_TABLE **>(reinterpret_cast<PUCHAR>(process) + 0x570);
        if (!pTable) {
            ObDereferenceObject(process);
            return;
        }

        KdPrint(("Handle table address: %llu\n", pTable->TableCode));

        ULONG_PTR tableBase = (pTable->TableCode & ~0x3ULL);
        ULONG level = (pTable->TableCode & 0x3ULL);
        auto index = static_cast<ULONG>(handleValue >> 2);
        HANDLE_TABLE_ENTRY* entry = nullptr;

        // Navigate handle table based on level
        if (level == 0) {
            // Single-level table
            entry = reinterpret_cast<HANDLE_TABLE_ENTRY*>(tableBase) + index;
        }
        else if (level == 1) {
            // Two-level table
            auto dir = reinterpret_cast<ULONG_PTR*>(tableBase);
            auto subTable = reinterpret_cast<HANDLE_TABLE_ENTRY*>(dir[index >> 10]);
            if (subTable) {
                entry = subTable + (index & 0x3FF);
            }
        }
        else if (level == 2) {
            // Three-level table
            auto dir1 = reinterpret_cast<ULONG_PTR*>(tableBase);
            auto dir2 = reinterpret_cast<ULONG_PTR*>(dir1[index >> 19]);
            if (dir2) {
                auto subTable = reinterpret_cast<HANDLE_TABLE_ENTRY*>(dir2[(index >> 10) & 0x1FF]);
                if (subTable) {
                    entry = subTable + (index & 0x3FF);
                }
            }
        }

        if (entry) {
            KdPrint(("GrantedAccessBits: %lu\n", entry->GrantedAccessBits));
            KdPrint(("Attributes: %u\n", entry->Attributes));
            KdPrint(("RefCount: %u\n", entry->RefCount));

            // Grant full access
            entry->GrantedAccessBits = PROCESS_ALL_ACCESS;
            KdPrint(("Handle access granted\n"));
        } else {
            KdPrint(("Failed to find handle entry\n"));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        KdPrint(("Exception while modifying handle table\n"));
    }

    if (process) {
        ObDereferenceObject(process);
    }
}




void GrantHandleAccess(XWW_REQUEST* request) {
    if (!request) {
        KdPrint(("Invalid request structure\n"));
        return;
    }

    if (request->Handle == 0) {
        KdPrint(("Invalid handle value\n"));
        return;
    }

    KdPrint(("Modifying handle: 0x%llX\n", request->Handle));
    ElevateHandleAccess(request->Handle);
    KdPrint(("Handle modification completed\n"));
}
