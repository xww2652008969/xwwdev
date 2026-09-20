#include "Devices.h"
#include <winsvc.h>
#include <string>

namespace
{
	// 构造 IOCTL 缓冲区：固定头部 + 紧随其后的变长数据区
	std::vector<uint8_t> BuildRequest(const XWW_REQUEST& header, const void* payload, size_t payloadSize)
	{
		std::vector<uint8_t> ioBuffer(XWW_REQUEST_HEADER_SIZE + payloadSize);
		memcpy(ioBuffer.data(), &header, XWW_REQUEST_HEADER_SIZE);
		if (payload != nullptr && payloadSize != 0) {
			memcpy(ioBuffer.data() + XWW_REQUEST_HEADER_SIZE, payload, payloadSize);
		}
		return ioBuffer;
	}

	// 发送请求，响应原地写回同一缓冲区（METHOD_BUFFERED）
	bool SendRequest(HANDLE hDevice, std::vector<uint8_t>& ioBuffer)
	{
		DWORD bytesReturned = 0;
		BOOL ok = DeviceIoControl(
			hDevice,
			IOCTL_XWW_REQUEST,
			ioBuffer.data(),
			static_cast<DWORD>(ioBuffer.size()),
			ioBuffer.data(),
			static_cast<DWORD>(ioBuffer.size()),
			&bytesReturned,
			nullptr
		);
		return ok == TRUE;
	}

	// 取变长数据区起始位置
	uint8_t* PayloadPtr(std::vector<uint8_t>& ioBuffer)
	{
		return ioBuffer.data() + XWW_REQUEST_HEADER_SIZE;
	}
}

/// <summary>
/// 附加写入：把 buffer 中 size 字节写入目标进程的 targetAddress
/// </summary>
extern "C" __declspec(dllexport)
bool WriteAttach(HANDLE hDevice, uint32_t pid, uint64_t targetAddress, const uint8_t* buffer, int size)
{
	if (size < 1 || buffer == nullptr)
		return false;

	XWW_REQUEST request{};
	request.Operation     = XWW_OP_WRITE_MEMORY;
	request.ProcessId     = pid;
	request.TargetAddress = targetAddress;
	request.BufferLength  = static_cast<uint32_t>(size);

	auto ioBuffer = BuildRequest(request, buffer, static_cast<size_t>(size));
	return SendRequest(hDevice, ioBuffer);
}

/// <summary>
/// 附加读取：从目标进程 targetAddress 读取 dataLength 字节到 buffer
/// </summary>
extern "C" __declspec(dllexport)
bool ReadAttach(HANDLE hDevice, uint32_t pid, uint64_t targetAddress, int dataLength, uint8_t* buffer)
{
	if (dataLength < 1 || buffer == nullptr)
		return false;

	XWW_REQUEST request{};
	request.Operation     = XWW_OP_READ_MEMORY;
	request.ProcessId     = pid;
	request.TargetAddress = targetAddress;
	request.BufferLength  = static_cast<uint32_t>(dataLength);

	auto ioBuffer = BuildRequest(request, nullptr, static_cast<size_t>(dataLength));
	if (!SendRequest(hDevice, ioBuffer))
		return false;

	memcpy(buffer, PayloadPtr(ioBuffer), static_cast<size_t>(dataLength));
	return true;
}

/// <summary>
/// 内核打开进程：由驱动调用 ZwOpenProcess，句柄经数据区返回
/// </summary>
extern "C" __declspec(dllexport)
bool KernelOpenProcess(HANDLE hDevice, uint32_t pid, HANDLE& hProcess)
{
	hProcess = nullptr;
	if (pid == 0)
		return false;

	XWW_REQUEST request{};
	request.Operation     = XWW_OP_OPEN_PROCESS_HANDLE;
	request.ProcessId     = pid;
	request.TargetAddress = 0;
	request.BufferLength  = sizeof(HANDLE);  // 驱动要求 >= sizeof(HANDLE)

	auto ioBuffer = BuildRequest(request, nullptr, sizeof(HANDLE));
	if (!SendRequest(hDevice, ioBuffer))
		return false;

	memcpy(&hProcess, PayloadPtr(ioBuffer), sizeof(HANDLE));
	return true;
}

/// <summary>
/// 内核打开线程：由驱动调用 ZwOpenThread，线程句柄经数据区返回。
/// 注意：驱动当前实现（process.cpp）是从 Handle 字段取 TID 的，ThreadId 字段暂未被读取，
/// 故这里两个字段都填同一个 tid，驱动改成读 ThreadId 后无需再动用户态。
/// </summary>
extern "C" __declspec(dllexport)
bool KernelOpenThread(HANDLE hDevice, uint32_t pid, uint32_t tid, HANDLE& hThread)
{
	hThread = nullptr;
	if (pid == 0 || tid == 0)
		return false;

	XWW_REQUEST request{};
	request.Operation     = XWW_OP_OPEN_THREAD_HANDLE;
	request.ProcessId     = pid;
	request.ThreadId      = tid;              // 协议字段（驱动后续版本使用）
	request.Handle        = tid;              // 驱动当前实现取 TID 的位置
	request.BufferLength  = sizeof(HANDLE);   // 驱动要求 >= sizeof(HANDLE)

	auto ioBuffer = BuildRequest(request, nullptr, sizeof(HANDLE));
	if (!SendRequest(hDevice, ioBuffer))
		return false;

	memcpy(&hThread, PayloadPtr(ioBuffer), sizeof(HANDLE));
	return hThread != nullptr;
}

/// <summary>
/// 句柄提权：将驱动遍历句柄表，把 handle 的访问权限提升为完全访问
/// 驱动侧仅使用 Handle 字段，ProcessId / TargetAddress 不参与处理
/// </summary>
extern "C" __declspec(dllexport)
bool GrantHandleAccess(HANDLE hDevice, uint64_t handle)
{
	if (handle == 0)
		return false;  // 驱动对 Handle == 0 直接返回，不做任何处理

	XWW_REQUEST request{};
	request.Operation    = XWW_OP_GRANT_HANDLE_ACCESS;
	request.Handle       = handle;
	request.BufferLength = sizeof(uint64_t);  // 仅用于满足驱动的最小长度校验

	auto ioBuffer = BuildRequest(request, nullptr, sizeof(uint64_t));
	return SendRequest(hDevice, ioBuffer);
}

/// <summary>
/// 在目标进程申请内存，分配的基址由驱动写入 Handle 字段返回
/// </summary>
extern "C" __declspec(dllexport)
bool AllocateRemoteMemory(HANDLE hDevice, uint32_t pid, uint32_t size, uint64_t& remoteBase)
{
	remoteBase = 0;
	if (pid == 0 || size == 0)
		return false;

	XWW_REQUEST request{};
	request.Operation    = XWW_OP_ALLOCATE_MEMORY;
	request.ProcessId    = pid;
	request.AllocateSize = size;

	auto ioBuffer = BuildRequest(request, nullptr, sizeof(uint64_t));
	if (!SendRequest(hDevice, ioBuffer))
		return false;

	// 基址写回请求头的 Handle 字段，需重新按结构体解析头部
	XWW_REQUEST response{};
	memcpy(&response, ioBuffer.data(), XWW_REQUEST_HEADER_SIZE);
	remoteBase = response.Handle;
	return remoteBase != 0;
}

/// <summary>
/// 释放目标进程中由 AllocateRemoteMemory 申请的内存
/// </summary>
extern "C" __declspec(dllexport)
bool FreeRemoteMemory(HANDLE hDevice, uint32_t pid, uint64_t remoteBase)
{
	if (pid == 0 || remoteBase == 0)
		return false;

	XWW_REQUEST request{};
	request.Operation     = XWW_OP_FREE_MEMORY;
	request.ProcessId     = pid;
	request.TargetAddress = remoteBase;

	auto ioBuffer = BuildRequest(request, nullptr, 0);
	return SendRequest(hDevice, ioBuffer);
}

/// <summary>
/// 打开驱动设备
/// </summary>
extern "C" __declspec(dllexport)
HANDLE OpenDevices()
{
	HANDLE h = CreateFileW(
		XWW_DEVICE_PATH,                // \\.\xww
		GENERIC_READ | GENERIC_WRITE,   // 读写权限
		0,                              // 不共享
		nullptr,                        // 默认安全属性
		OPEN_EXISTING,                  // 打开已存在设备
		0,                              // 文件属性和标志
		nullptr                         // 模板文件句柄
	);

	if (h != INVALID_HANDLE_VALUE)
		return h;
	return nullptr;
}

// ============================================================
// 驱动加载 / 卸载
// ============================================================
namespace
{
	constexpr wchar_t kDriverServiceName[] = L"xww";      // 服务名
	constexpr wchar_t kDriverFileName[]    = L"xww.sys";  // 驱动文件名

	DWORD g_driverLastError = 0;  // 最近一次加载 / 卸载的失败错误码

	// 取 wtool.dll 自身所在目录（末尾不含反斜杠）
	bool GetModuleDirectory(std::wstring& outDir)
	{
		HMODULE hModule = nullptr;
		// 借助本函数地址反查所属模块，无需依赖 DllMain 保存的实例句柄
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&GetModuleDirectory),
				&hModule)) {
			return false;
		}

		WCHAR path[MAX_PATH]{};
		if (GetModuleFileNameW(hModule, path, MAX_PATH) == 0)
			return false;

		const std::wstring full(path);
		const auto pos = full.find_last_of(L"\\/");
		if (pos == std::wstring::npos)
			return false;

		outDir = full.substr(0, pos);
		return true;
	}

	// 输出诊断信息（DebugView / VS 输出窗口可见）
	void TraceDriver(const wchar_t* action, DWORD err)
	{
		WCHAR line[640]{};
		if (err == 0)
			swprintf_s(line, L"[wtool] %s 成功\n", action);
		else
			swprintf_s(line, L"[wtool] %s 失败, GetLastError=%lu\n", action, err);
		OutputDebugStringW(line);
	}

	// 轮询等待服务进入指定状态。
	// 注意：ControlService / StartService 都只是投递请求，必须靠轮询确认真实状态，
	// 否则后续 DeleteService 会把服务永久卡在"待删除"(ERROR_SERVICE_MARKED_FOR_DELETE)。
	bool WaitForServiceState(SC_HANDLE hService, DWORD desiredState, DWORD timeoutMs)
	{
		constexpr DWORD kInterval = 100;
		for (DWORD waited = 0; waited < timeoutMs; waited += kInterval) {
			SERVICE_STATUS status{};
			if (!QueryServiceStatus(hService, &status))
				return false;
			if (status.dwCurrentState == desiredState)
				return true;
			Sleep(kInterval);
		}
		return false;
	}

	// CreateService 返回"已存在 / 待删除"时调用：
	// 能打开就更新驱动路径后复用；发现已被 SCM 清理干净则重新创建；
	// 仍处于待删除则等待 SCM 完成清理后重试。
	SC_HANDLE OpenOrRecreateService(SC_HANDLE hScm, const std::wstring& sysPath, DWORD waitMs)
	{
		constexpr DWORD kInterval = 200;
		for (DWORD waited = 0; ; waited += kInterval) {
			SC_HANDLE h = OpenServiceW(hScm, kDriverServiceName,
				SERVICE_START | SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
			if (h != nullptr) {
				// 复用现有服务：更新二进制路径，避免仍指向旧位置的 sys
				ChangeServiceConfigW(
					h,
					SERVICE_KERNEL_DRIVER,
					SERVICE_DEMAND_START,
					SERVICE_ERROR_NORMAL,
					sysPath.c_str(),
					nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
				return h;
			}

			const DWORD err = GetLastError();
			if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
				// SCM 已清理干净，重新创建
				return CreateServiceW(
					hScm,
					kDriverServiceName,
					kDriverServiceName,
					SERVICE_START | SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS,
					SERVICE_KERNEL_DRIVER,
					SERVICE_DEMAND_START,
					SERVICE_ERROR_NORMAL,
					sysPath.c_str(),
					nullptr, nullptr, nullptr, nullptr, nullptr);
			}

			if (err != ERROR_SERVICE_MARKED_FOR_DELETE)
				return nullptr;             // 其它错误，没有重试意义

			if (waited >= waitMs) {
				SetLastError(ERROR_SERVICE_MARKED_FOR_DELETE);
				return nullptr;             // 等待超时，仍卡在待删除
			}
			Sleep(kInterval);
		}
	}
}

/// <summary>
/// 加载驱动：在 DLL 同目录查找 xww.sys，注册为服务名 xww 并启动。
/// 不做签名校验；服务已存在时先更新其二进制路径再启动；已在运行视为成功。
/// </summary>
extern "C" __declspec(dllexport)
bool LoadDriver()
{
	g_driverLastError = 0;

	// 定位 DLL 所在目录下的 xww.sys
	std::wstring dir;
	if (!GetModuleDirectory(dir)) {
		g_driverLastError = GetLastError();
		TraceDriver(L"获取 DLL 所在目录", g_driverLastError);
		return false;
	}

	const std::wstring sysPath = dir + L"\\" + kDriverFileName;
	if (GetFileAttributesW(sysPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
		g_driverLastError = ERROR_FILE_NOT_FOUND;
		TraceDriver((std::wstring(L"未找到驱动文件 ") + sysPath).c_str(), g_driverLastError);
		return false;
	}

	// 打开服务控制管理器（创建服务需要 SC_MANAGER_CREATE_SERVICE）
	SC_HANDLE hScm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
	if (hScm == nullptr) {
		g_driverLastError = GetLastError();
		TraceDriver(L"OpenSCManager", g_driverLastError);
		return false;
	}

	// 创建内核驱动服务
	SC_HANDLE hService = CreateServiceW(
		hScm,
		kDriverServiceName,                 // 服务名
		kDriverServiceName,                 // 显示名
		SERVICE_START | SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS,
		SERVICE_KERNEL_DRIVER,              // 内核驱动
		SERVICE_DEMAND_START,               // 手动启动
		SERVICE_ERROR_NORMAL,
		sysPath.c_str(),                    // 驱动文件完整路径
		nullptr, nullptr, nullptr, nullptr, nullptr);

	if (hService == nullptr) {
		const DWORD err = GetLastError();
		// 1073 已存在 / 1072 上次卸载后 SCM 仍在清理，等待其完成后复用或重建
		if (err == ERROR_SERVICE_EXISTS || err == ERROR_SERVICE_MARKED_FOR_DELETE) {
			hService = OpenOrRecreateService(hScm, sysPath, 10000);
		}
		if (hService == nullptr) {
			g_driverLastError = GetLastError();
			TraceDriver(L"创建 / 打开服务", g_driverLastError);
			if (g_driverLastError == ERROR_SERVICE_MARKED_FOR_DELETE) {
				TraceDriver(L"服务仍卡在待删除状态，需删除残留注册表项 "
							L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\xww 或重启", 0);
			}
			CloseServiceHandle(hScm);
			return false;
		}
	}

	// 启动服务（即加载驱动）
	bool started = false;
	if (StartServiceW(hService, 0, nullptr)) {
		// StartService 也只是投递请求，等驱动真正进入 RUNNING
		started = WaitForServiceState(hService, SERVICE_RUNNING, 15000);
		if (!started) {
			g_driverLastError = ERROR_SERVICE_REQUEST_TIMEOUT;
			TraceDriver(L"等待服务运行超时(15s)", g_driverLastError);
		}
	} else {
		const DWORD err = GetLastError();
		if (err == ERROR_SERVICE_ALREADY_RUNNING) {
			started = true;  // 已在运行
		} else {
			g_driverLastError = err;
			// 未签名驱动通常会得到 ERROR_INVALID_IMAGE_HASH(577)
			TraceDriver(L"StartService", err);
		}
	}

	if (started) {
		g_driverLastError = 0;
		TraceDriver(L"加载驱动", 0);
	}

	CloseServiceHandle(hService);
	CloseServiceHandle(hScm);
	return started;
}

/// <summary>
/// 卸载驱动：停止并删除 xww 服务
/// </summary>
extern "C" __declspec(dllexport)
bool UnloadDriver()
{
	g_driverLastError = 0;

	SC_HANDLE hScm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
	if (hScm == nullptr) {
		g_driverLastError = GetLastError();
		TraceDriver(L"OpenSCManager", g_driverLastError);
		return false;
	}

	SC_HANDLE hService = OpenServiceW(hScm, kDriverServiceName,
		SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
	if (hService == nullptr) {
		const DWORD err = GetLastError();
		CloseServiceHandle(hScm);
		if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
			g_driverLastError = 0;
			return true;  // 服务本就不存在，视为已卸载
		}
		g_driverLastError = err;
		TraceDriver(L"OpenService", err);
		return false;
	}

	// 停止服务。
	// ControlService 成功仅代表控制码已投递，驱动未必已卸载完毕，
	// 必须轮询到 SERVICE_STOPPED 才能删除，否则服务会永久停在"待删除"状态。
	bool stopped = false;
	SERVICE_STATUS status{};
	if (QueryServiceStatus(hService, &status) && status.dwCurrentState == SERVICE_STOPPED) {
		stopped = true;  // 本就未运行
	} else if (ControlService(hService, SERVICE_CONTROL_STOP, &status)) {
		stopped = WaitForServiceState(hService, SERVICE_STOPPED, 15000);
		if (!stopped) {
			g_driverLastError = ERROR_SERVICE_REQUEST_TIMEOUT;
			TraceDriver(L"等待服务停止超时(15s)，驱动可能仍被占用", g_driverLastError);
		}
	} else {
		const DWORD err = GetLastError();
		if (err == ERROR_SERVICE_NOT_ACTIVE) {
			stopped = true;  // 本就未运行
		} else {
			g_driverLastError = err;
			TraceDriver(L"停止服务", err);
		}
	}

	// 只有确认已停止才删除；未停止就删除会把服务卡死在待删除状态
	bool deleted = false;
	if (stopped) {
		// SCM 关闭内部句柄存在竞态，刚停止时 DeleteService 可能短暂失败，重试若干次
		constexpr int kDeleteRetries = 20;   // 20 * 200ms = 4s
		for (int i = 0; i < kDeleteRetries; ++i) {
			if (DeleteService(hService)) {
				deleted = true;
				break;
			}

			const DWORD err = GetLastError();
			if (err == ERROR_SERVICE_MARKED_FOR_DELETE) {
				// 已在删除队列中，等价目标状态，不算失败
				deleted = true;
				break;
			}
			if (err != ERROR_ACCESS_DENIED && err != ERROR_INVALID_HANDLE &&
				err != ERROR_SERVICE_CANNOT_ACCEPT_CTRL) {
				g_driverLastError = err;    // 其它错误重试无意义
				break;
			}

			g_driverLastError = err;
			Sleep(200);
		}

		if (!deleted)
			TraceDriver(L"删除服务", g_driverLastError);
	} else {
		TraceDriver(L"服务未停止，跳过删除以避免残留", 0);
	}

	if (stopped && deleted) {
		g_driverLastError = 0;
		TraceDriver(L"卸载驱动", 0);
	}

	CloseServiceHandle(hService);
	CloseServiceHandle(hScm);
	return stopped && deleted;
}

/// <summary>
/// 返回最近一次 LoadDriver / UnloadDriver 的失败错误码，成功时为 0
/// </summary>
extern "C" __declspec(dllexport)
DWORD GetDriverLastError()
{
	return g_driverLastError;
}
