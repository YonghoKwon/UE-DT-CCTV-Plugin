param([string]$Out='')
$ErrorActionPreference='Stop'
if(-not ('DTCctvInputDesktopProbe' -as [type]))
{
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class DTCctvInputDesktopProbe
{
    [DllImport("user32.dll", SetLastError=true)] static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
    [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Unicode)] static extern bool GetUserObjectInformation(IntPtr handle, int index, StringBuilder value, uint length, out uint needed);
    [DllImport("user32.dll", SetLastError=true)] static extern bool CloseDesktop(IntPtr handle);
    public static string ReadName()
    {
        IntPtr desktop=OpenInputDesktop(0,false,1);
        if(desktop==IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        try
        {
            uint needed;
            var value=new StringBuilder(512);
            if(!GetUserObjectInformation(desktop,2,value,1024,out needed)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            return value.ToString();
        }
        finally { CloseDesktop(desktop); }
    }
}
'@
}
$result=[ordered]@{status='ENV_BLOCKED';checked_at=(Get-Date -Format o);method='user32 OpenInputDesktop / GetUserObjectInformation(UOI_NAME)';name='';session_id=(Get-Process -Id $PID).SessionId;note='LogonUI 프로세스 유무는 잠금 판단에 사용하지 않음.'}
try
{
    $result.name=[DTCctvInputDesktopProbe]::ReadName()
    if($result.name -eq 'Default'){$result.status='PASS'}
}
catch{$result.note='입력 데스크톱 조회 실패: '+$_.Exception.Message}
$json=$result|ConvertTo-Json
if($Out){[IO.File]::WriteAllText([IO.Path]::GetFullPath($Out),$json,[Text.UTF8Encoding]::new($false))}
$json
