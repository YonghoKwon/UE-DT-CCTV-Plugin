if(-not ('DTCctvWindowNative' -as [type]))
{
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class DTCctvWindowNative
{
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct Power { public uint Version,ControlMask,StateMask; }
    public sealed class PowerResult { public bool Ok; public int Error; public uint Version,ControlMask,StateMask; }
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] static extern bool AttachThreadInput(uint a,uint b,bool attach);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,int pid);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessInformation(IntPtr handle,int kind,ref Power state,uint size);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetProcessInformation(IntPtr handle,int kind,ref Power state,uint size);
    public static uint ForegroundPid()
    {
        uint pid; GetWindowThreadProcessId(GetForegroundWindow(),out pid); return pid;
    }
    public static bool Foreground(IntPtr target)
    {
        uint pid;
        uint other=GetWindowThreadProcessId(GetForegroundWindow(),out pid);
        uint current=GetCurrentThreadId();
        bool attached=other!=0 && other!=current && AttachThreadInput(current,other,true);
        try { BringWindowToTop(target); return SetForegroundWindow(target); }
        finally { if(attached) AttachThreadInput(current,other,false); }
    }
    public static PowerResult ReadPower(int pid)
    {
        var result=new PowerResult();
        IntPtr handle=OpenProcess(0x1400,false,pid);
        if(handle==IntPtr.Zero) { result.Error=Marshal.GetLastWin32Error(); return result; }
        try
        {
            Power state=new Power(); state.Version=1;
            result.Ok=GetProcessInformation(handle,4,ref state,12);
            result.Error=result.Ok?0:Marshal.GetLastWin32Error();
            result.Version=state.Version;result.ControlMask=state.ControlMask;result.StateMask=state.StateMask;
            return result;
        }
        finally { CloseHandle(handle); }
    }
    public static PowerResult WritePower(int pid,uint control,uint state)
    {
        IntPtr handle=OpenProcess(0x1600,false,pid);
        if(handle==IntPtr.Zero) return new PowerResult{Error=Marshal.GetLastWin32Error()};
        try
        {
            Power value=new Power{Version=1,ControlMask=control,StateMask=state};
            if(!SetProcessInformation(handle,4,ref value,12)) return new PowerResult{Error=Marshal.GetLastWin32Error()};
        }
        finally { CloseHandle(handle); }
        return ReadPower(pid);
    }
}
'@
}
