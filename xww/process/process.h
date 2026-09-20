#pragma once
#include <ntifs.h>
#include <ntddk.h>
#include "../driver/driver.h"

EXTERN_C NTSTATUS OpenProcessHandle(XWW_REQUEST* request);

EXTERN_C NTSTATUS OpenThreadHandle(XWW_REQUEST* request);