using System.ComponentModel;
using System.Runtime.InteropServices;

namespace te;

/// <summary>
/// wtool.dll 全部导出函数的 P/Invoke 封装。
/// 项目开启了 IsAotCompatible，故使用 LibraryImport（编译期源生成）而非 DllImport。
/// wtool.dll 需与本程序可执行文件位于同一目录（构建已配置输出到 ..\..\out）。
/// </summary>
/// <remarks>
/// 原生函数均为 extern "C" 导出，返回 C++ bool（1 字节），
/// 因此返回值统一标注 MarshalAs(UnmanagedType.I1)，否则默认按 4 字节 Win32 BOOL 处理。
/// </remarks>
internal static unsafe partial class Wtool
{
    private const string Dll = "wtool.dll";

    // ==================================================================
    // 原生声明（一一对应 wtool 的 extern "C" 导出）
    // ==================================================================

    [LibraryImport(Dll, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.I1)]
    internal static partial bool WriteAttach(IntPtr hDevice, uint pid, ulong targetAddress, byte* buffer, int size);

    [LibraryImport(Dll, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.I1)]
    internal static partial bool ReadAttach(IntPtr hDevice, uint pid, ulong targetAddress, int dataLength, byte* buffer);

    [LibraryImport(Dll, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.I1)]
    internal static partial bool KernelOpenProcess(IntPtr hDevice, uint pid, out IntPtr hProcess);

    [LibraryImport(Dll, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.I1)]
    internal static partial bool KernelOpenThread(IntPtr hDevice, uint pid, uint tid, out IntPtr hThread);

    [LibraryImport(Dll, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.I1)]
    internal static partial bool GrantHandleAccess(IntPtr hDevice, ulong handle);

    [LibraryImport(Dll, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.I1)]
    internal static partial bool AllocateRemoteMemory(IntPtr hDevice, uint pid, uint size, out ulong remoteBase);

    [LibraryImport(Dll, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.I1)]
    internal static partial bool FreeRemoteMemory(IntPtr hDevice, uint pid, ulong remoteBase);

    [LibraryImport(Dll, SetLastError = true)]
    internal static partial IntPtr OpenDevices();

    [LibraryImport(Dll, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.I1)]
    internal static partial bool LoadDriver();

    [LibraryImport(Dll, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.I1)]
    internal static partial bool UnloadDriver();

    [LibraryImport(Dll)]
    internal static partial uint GetDriverLastError();

    /// <summary>返回目标进程中 DLL 的基址，失败返回 IntPtr.Zero。</summary>
    [LibraryImport(Dll, SetLastError = true)]
    internal static partial IntPtr Mapdll(IntPtr hProc, IntPtr dllData, nuint fileSize, uint pid);
    
    [LibraryImport(Dll, SetLastError = true)]
    public static partial void RemoteCall(IntPtr hProc,IntPtr dllData, uint pid);

    // ==================================================================
    // 托管友好包装：失败时抛异常，无需逐个检查返回值
    // ==================================================================

    /// <summary>加载驱动：从本程序所在目录查找 xww.sys，注册服务名 xww 并启动。需管理员权限。</summary>
    public static void Load()
    {
        if (LoadDriver()) return;

        uint err = GetDriverLastError();
        throw new Win32Exception((int)err, "加载驱动失败。" + ExplainDriverError(err));
    }

    /// <summary>
    /// 卸载驱动：停止并删除 xww 服务。
    /// 重要：调用前必须先用 <see cref="Close"/> 关闭设备句柄及所有驱动返回的句柄，
    /// 否则驱动卸载不干净，服务会残留为待删除状态（错误码 1072）。
    /// </summary>
    public static void Unload()
    {
        if (UnloadDriver()) return;

        uint err = GetDriverLastError();
        throw new Win32Exception((int)err, "卸载驱动失败。" + ExplainDriverError(err));
    }

    /// <summary>
    /// 关闭设备句柄或驱动返回的进程句柄。卸载驱动前必须关闭，否则驱动无法真正卸载。
    /// </summary>
    public static void Close(IntPtr handle)
    {
        if (handle != IntPtr.Zero)
            CloseHandle(handle);
    }

    [LibraryImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.I1)]
    private static partial bool CloseHandle(IntPtr hObject);

    /// <summary>打开 \\.\xww 设备，失败返回 IntPtr.Zero（驱动未加载时常见）。</summary>
    public static IntPtr Open()
    {
        var h = OpenDevices();
        if (h == IntPtr.Zero)
            throw new Win32Exception(Marshal.GetLastWin32Error(), @"打开 \\.\xww 失败，确认驱动已加载。");
        return h;
    }

    /// <summary>由驱动打开目标进程，返回内核句柄。</summary>
    public static IntPtr OpenProcess(IntPtr hDevice, uint pid)
    {
        if (!KernelOpenProcess(hDevice, pid, out IntPtr hProcess) || hProcess == IntPtr.Zero)
            throw new Win32Exception(Marshal.GetLastWin32Error(), $"内核打开进程失败，pid={pid}。");
        return hProcess;
    }

    /// <summary>由驱动打开目标线程，返回线程句柄。</summary>
    public static IntPtr OpenThread(IntPtr hDevice, uint pid, uint tid)
    {
        if (!KernelOpenThread(hDevice, pid, tid, out IntPtr hThread) || hThread == IntPtr.Zero)
            throw new Win32Exception(Marshal.GetLastWin32Error(), $"内核打开线程失败，pid={pid}, tid={tid}。");
        return hThread;
    }

    /// <summary>句柄提权，handle 可为进程或线程句柄。</summary>
    public static void Grant(IntPtr hDevice, IntPtr handle)
    {
        if (!GrantHandleAccess(hDevice, (ulong)handle.ToInt64()))
            throw new Win32Exception(Marshal.GetLastWin32Error(), "句柄提权失败。");
    }

    /// <summary>读取目标进程内存，返回字节数组。</summary>
    public static byte[] Read(IntPtr hDevice, uint pid, ulong address, int length)
    {
        if (length <= 0) throw new ArgumentOutOfRangeException(nameof(length));

        byte[] buffer = new byte[length];
        fixed (byte* p = buffer)
        {
            if (!ReadAttach(hDevice, pid, address, length, p))
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    $"读取内存失败，pid={pid}, address=0x{address:X}。");
        }
        return buffer;
    }

    /// <summary>向目标进程写入内存。</summary>
    public static void Write(IntPtr hDevice, uint pid, ulong address, byte[] data)
    {
        ArgumentNullException.ThrowIfNull(data);
        if (data.Length == 0) throw new ArgumentException("数据不能为空。", nameof(data));

        fixed (byte* p = data)
        {
            if (!WriteAttach(hDevice, pid, address, p, data.Length))
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    $"写入内存失败，pid={pid}, address=0x{address:X}。");
        }
    }

    /// <summary>读取一个非托管值类型（int / float / long / 自定义结构体等）。</summary>
    public static T ReadValue<T>(IntPtr hDevice, uint pid, ulong address) where T : unmanaged
    {
        T value;
        if (!ReadAttach(hDevice, pid, address, sizeof(T), (byte*)&value))
            throw new Win32Exception(Marshal.GetLastWin32Error(),
                $"读取 {typeof(T).Name} 失败，pid={pid}, address=0x{address:X}。");
        return value;
    }

    /// <summary>写入一个非托管值类型（int / float / long / 自定义结构体等）。</summary>
    public static void WriteValue<T>(IntPtr hDevice, uint pid, ulong address, T value) where T : unmanaged
    {
        if (!WriteAttach(hDevice, pid, address, (byte*)&value, sizeof(T)))
            throw new Win32Exception(Marshal.GetLastWin32Error(),
                $"写入 {typeof(T).Name} 失败，pid={pid}, address=0x{address:X}。");
    }

    /// <summary>在目标进程申请内存，返回分配到的基址。</summary>
    public static ulong Alloc(IntPtr hDevice, uint pid, uint size)
    {
        if (!AllocateRemoteMemory(hDevice, pid, size, out ulong remoteBase) || remoteBase == 0)
            throw new Win32Exception(Marshal.GetLastWin32Error(),
                $"申请远程内存失败，pid={pid}, size={size}。");
        return remoteBase;
    }

    /// <summary>释放目标进程中已申请的内存。</summary>
    public static void Free(IntPtr hDevice, uint pid, ulong remoteBase)
    {
        if (!FreeRemoteMemory(hDevice, pid, remoteBase))
            throw new Win32Exception(Marshal.GetLastWin32Error(),
                $"释放远程内存失败，pid={pid}, address=0x{remoteBase:X}。");
    }

    /// <summary>
    /// 手动映射注入 DLL，dllBytes 为磁盘上读取的完整 DLL 文件字节。
    /// 返回 DLL 在目标进程中的基址；失败抛异常。
    /// </summary>
    public static IntPtr MapDll(IntPtr hProc, byte[] dllBytes, uint pid)
    {
        ArgumentNullException.ThrowIfNull(dllBytes);
        if (dllBytes.Length == 0) throw new ArgumentException("DLL 数据不能为空。", nameof(dllBytes));

        var hGlobal = Marshal.AllocHGlobal(dllBytes.Length);
        try
        {
            Marshal.Copy(dllBytes, 0, hGlobal, dllBytes.Length);

            var hMod = Mapdll(hProc, hGlobal, (nuint)dllBytes.Length, pid);
            if (hMod == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error(), $"注入 DLL 失败，pid={pid}。");

            return hMod;
        }
        finally
        {
            Marshal.FreeHGlobal(hGlobal);
        }
    }


    /// <summary>把驱动加载的常见错误码翻译成可读说明。</summary>
    public static string ExplainDriverError(uint err) => err switch
    {
        5     => "错误码 5 = ERROR_ACCESS_DENIED，需以管理员身份运行。",
        2     => "错误码 2 = ERROR_FILE_NOT_FOUND，确认 xww.sys 与 wtool.dll 在同一目录。",
        577   => "错误码 577 = ERROR_INVALID_IMAGE_HASH，驱动签名不被接受，"
               + "需 bcdedit /set testsigning on 后重启。",
        1060  => "错误码 1060 = ERROR_SERVICE_DOES_NOT_EXIST。",
        1062  => "错误码 1062 = ERROR_SERVICE_NOT_ACTIVE，驱动未运行。",
        1072  => "错误码 1072 = ERROR_SERVICE_MARKED_FOR_DELETE，服务卡在待删除状态。"
               + "成因：上次卸载时驱动未真正停止（多半是设备句柄没关）就删除了服务。"
               + "修复：管理员身份执行 Remove-Item "
               + "'HKLM:\\SYSTEM\\CurrentControlSet\\Services\\xww' -Recurse -Force 后重试。",
        1073  => "错误码 1073 = ERROR_SERVICE_EXISTS（服务已存在，应走更新路径分支）。",
        1053  => "错误码 1053 = ERROR_SERVICE_REQUEST_TIMEOUT，驱动启动超时，"
               + "可能卡在 DriverEntry。",
        _     => $"错误码 {err}，详见 Windows 系统错误码。",
    };
}
