# probe_srv2.ps1 - deep-dive: why WM_SETTEXT to 3073 fails in tcp_server mode
$sig = @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W4 {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, [MarshalAs(UnmanagedType.LPWStr)]string s);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
Add-Type -TypeDefinition $sig

function Info([IntPtr]$main, [int]$id) {
    $h = [W4]::GetDlgItem($main, $id)
    if ($h -eq [IntPtr]::Zero) { return "id=$id NULL" }
    $cls = New-Object System.Text.StringBuilder 64
    [void][W4]::GetClassNameW($h, $cls, 64)
    $txt = New-Object System.Text.StringBuilder 256
    [void][W4]::GetWindowTextW($h, $txt, 256)
    $r = New-Object W4+RECT
    [void][W4]::GetWindowRect($h, [ref]$r)
    ("id=$id cls=" + $cls.ToString() + " text=[" + $txt.ToString() + "] rect=" + $r.L + "," + $r.T + "-" + $r.R + "," + $r.B +
     " vis=" + [W4]::IsWindowVisible($h) + " en=" + [W4]::IsWindowEnabled($h))
}

$exe = Join-Path $PSScriptRoot "..\dist\sncom.exe"
$p = Start-Process -FilePath $exe -PassThru -WorkingDirectory (Split-Path $exe)
$h = [IntPtr]::Zero
for ($i=0; $i -lt 30; $i++) { Start-Sleep -Milliseconds 300; $q = Get-Process -Id $p.Id -EA SilentlyContinue; if ($q -and $q.MainWindowHandle -ne 0) { $h = $q.MainWindowHandle; break } }
Write-Output ("PID=" + $p.Id + " HWND=" + $h)

[void][W4]::SendMessageW($h, 0x0111, [IntPtr]2002, [IntPtr]::Zero)   # tcp_server
Start-Sleep -Milliseconds 800
Write-Output (Info $h 3073)
Write-Output (Info $h 3065)

# try set BOTH
[void][W4]::SendMessageW([W4]::GetDlgItem($h, 3073), 0x000C, [IntPtr]::Zero, "8080")
[void][W4]::SendMessageW([W4]::GetDlgItem($h, 3065), 0x000C, [IntPtr]::Zero, "9999")
Start-Sleep -Milliseconds 300
Write-Output ("after-set 3073: " + (Info $h 3073))
Write-Output ("after-set 3065: " + (Info $h 3065))

# click listen
[void][W4]::SendMessageW([W4]::GetDlgItem($h, 3066), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Milliseconds 1000
Write-Output "NETSTAT (exact PID column):"
netstat -ano | Select-String "\s$($p.Id)\s*$" | ForEach-Object { Write-Output ("  " + $_.ToString().Trim()) }
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
