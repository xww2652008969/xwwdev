#pragma once
#include <ntifs.h>
#include <intrin.h>

// Memory protection utilities
EXTERN_C VOID MmEnableWP();
EXTERN_C VOID MmDisableWP();
EXTERN_C VOID MmWriteProtectOn(IN KIRQL Irql);
EXTERN_C KIRQL MmWriteProtectOff();
EXTERN_C PVOID GetNtRoutineAddress(IN PCWSTR name);

// Safe version that returns status
EXTERN_C NTSTATUS GetNtRoutineAddressSafe(IN PCWSTR name, OUT PVOID* routineAddress);