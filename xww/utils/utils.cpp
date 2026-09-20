#include "utils.h"

#include <ntstrsafe.h>

VOID MmEnableWP()
{
    SIZE_T cr0 = __readcr0();
    cr0 |= 0x10000;
    __writecr0(cr0);
}

VOID MmDisableWP()
{
    SIZE_T cr0 = __readcr0();
    cr0 &= ~((SIZE_T)1 << 16);
    __writecr0(cr0);
}

VOID MmWriteProtectOn(IN KIRQL Irql)
{
    SIZE_T cr0 = (SIZE_T)__readcr0();
    cr0 |= 0x10000;
#ifdef _AMD64_
    _enable();
#else
    __asm cli
#endif
    __writecr0(cr0);
    KeLowerIrql(Irql);
}

KIRQL MmWriteProtectOff()
{
    KIRQL irql = KeRaiseIrqlToDpcLevel();
    SIZE_T cr0 = (SIZE_T)__readcr0();
    cr0 &= ~((SIZE_T)1 << 16);
    __writecr0(cr0);
#ifdef _AMD64_
    _disable();
#else
    __asm sti
#endif
    return irql;
}
PVOID GetNtRoutineAddress(IN PCWSTR name)
{
    if (!name) {
        return nullptr;
    }

    UNICODE_STRING ustr;
    RtlInitUnicodeString(&ustr, name);
    return MmGetSystemRoutineAddress(&ustr);
}

NTSTATUS GetNtRoutineAddressSafe(IN PCWSTR name, OUT PVOID* routineAddress)
{
    if (!name || !routineAddress) {
        return STATUS_INVALID_PARAMETER;
    }

    *routineAddress = nullptr;

    UNICODE_STRING ustr;
    NTSTATUS status = RtlUnicodeStringInit(&ustr, name);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    *routineAddress = MmGetSystemRoutineAddress(&ustr);

    return (*routineAddress != nullptr) ? STATUS_SUCCESS : STATUS_PROCEDURE_NOT_FOUND;
}