#pragma once

#include <ntifs.h>
#include <ntddk.h>
#include "../driver/driver.h"

EXTERN_C NTSTATUS ReadProcessMemory(XWW_REQUEST* request);
EXTERN_C NTSTATUS WriteProcessMemory(XWW_REQUEST* request);

EXTERN_C NTSTATUS AllocateProcessMemory(XWW_REQUEST* request);
EXTERN_C NTSTATUS FreeProcessMemory(XWW_REQUEST* request);
