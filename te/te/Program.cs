using System.ComponentModel;
using System.Diagnostics;

namespace te;

internal static unsafe class Program
{
    public static void Main(string[] args)
    {
        
        var dev = Wtool.Open();
        string target = "nightreign";
        uint pid = GetPid(target);

        if (pid == 0)
        {
            Console.WriteLine($"未检测到进程 {target}.exe");
            return;
        }

        var process = Wtool.OpenProcess(dev, pid);


        var dllpath = @"D:\mycode\frida\build\gadget\x86_64\frida-gadget.dll";
        var dlldata=File.ReadAllBytes(dllpath);
        
        
        
        
        // 180000000
        var hMod = Wtool.MapDll(process, dlldata, pid);
        Console.WriteLine($"注入成功，目标进程内基址 = 0x{hMod.ToInt64():X}");

        Console.WriteLine("执行地址");
        

        // // 目标进程名可用命令行参数覆盖，默认 Notepad
        // string target = args.Length > 0 ? args[0] : "Notepad";
        //
        // try
        // {
        //     // 1. 加载驱动：从本程序所在目录查找 xww.sys，注册服务名 xww 并启动
        //     Console.WriteLine("[1] 加载驱动 xww.sys ...");
        //     Wtool.Load();
        //     Console.WriteLine("    驱动已加载");
        //
        //     // 2. 打开设备 \\.\xww
        //     Console.WriteLine(@"[2] 打开 \\.\xww ...");
        //     var dev = Wtool.Open();
        //     Console.WriteLine($"    设备句柄 = 0x{dev.ToInt64():X}");
        //
        //     // 3. 定位目标进程
        //     uint pid = GetPid(target);
        //     Console.WriteLine($"[3] 目标进程 {target}，pid = {pid}");
        //
        //     // 4. 由驱动打开目标进程
        //     IntPtr hProc = Wtool.OpenProcess(dev, pid);
        //     Console.WriteLine($"[4] 内核进程句柄 = 0x{hProc.ToInt64():X}");
        //
        //     // 5. 读取目标进程主模块 PE 头，校验 MZ
        //     DemoReadPeHeader(dev, pid);
        //
        //     // 6. 演示按类型读写（需要已知地址，这里仅展示用法）
        //     // int  hp = Wtool.ReadValue<int>(dev, pid, 0x7FF600001000ul);
        //     // Wtool.WriteValue(dev, pid, 0x7FF600001000ul, 999);
        //     // byte[] buf = Wtool.Read(dev, pid, 0x7FF600001000ul, 64);
        //     // Wtool.Write(dev, pid, 0x7FF600001000ul, new byte[] { 1, 2, 3, 4 });
        //
        //     Console.WriteLine("\n完成。");
        //     Console.WriteLine("输入 u 卸载驱动，其它键退出：");
        //     if (Console.ReadKey().KeyChar is 'u' or 'U')
        //     {
        //         // 必须先关闭所有句柄，否则驱动卸载不干净，服务会残留为待删除状态(1072)
        //         Wtool.Close(hProc);
        //         Wtool.Close(dev);
        //         Wtool.Unload();
        //         Console.WriteLine("\n[7] 驱动已卸载");
        //     }
        // }
        // catch (Win32Exception ex)
        // {
        //     Console.WriteLine($"\n[失败] {ex.Message}");
        //     Console.WriteLine($"       错误码 = {ex.NativeErrorCode}");
        // }
        // catch (Exception ex)
        // {
        //     Console.WriteLine($"\n[失败] {ex.Message}");
        // }
    }

    /// <summary>读取目标进程主模块头部两个字节，验证驱动读内存通路是否正常。</summary>
    private static void DemoReadPeHeader(IntPtr dev, uint pid)
    {
        try
        {
            using Process proc = Process.GetProcessById((int)pid);
            ulong baseAddress = (ulong)proc.MainModule!.BaseAddress.ToInt64();

            byte[] head = Wtool.Read(dev, pid, baseAddress, 2);
            bool isMz = head.Length == 2 && head[0] == 'M' && head[1] == 'Z';
            Console.WriteLine($"[5] 读取主模块 0x{baseAddress:X} -> '{(char)head[0]}{(char)head[1]}' " +
                              (isMz ? "(MZ 正确，读内存通路正常)" : "(内容异常)"));
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[5] 读取主模块跳过：{ex.Message}");
        }
    }

    /// <summary>按进程名取 PID，找不到时抛异常（原实现会在空引用上崩）。</summary>
    private static uint GetPid(string processName)
    {
        Process? proc = Process.GetProcessesByName(processName).FirstOrDefault();
        if (proc is null)
            throw new InvalidOperationException($"未找到进程 {processName}，请先启动它。");
        return (uint)proc.Id;
    }
}
