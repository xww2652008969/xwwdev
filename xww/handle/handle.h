#pragma once

#include "../driver/driver.h"

// 将 request->Handle 指定的句柄提权为 PROCESS_ALL_ACCESS
void GrantHandleAccess(XWW_REQUEST* request);

