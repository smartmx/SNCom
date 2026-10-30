# probe_net.ps1 - reproduce bug1: type 8080, click connect, check actual SYN target port
$sig = @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W {
    [DllImport("user32.dll")] public static extern IntPtr FindWindowW([MarshalAs(UnmanagedType.LPWStr)]string cls, [MarshalAs(UnmanagedType.LPWStr)]string title);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, [MarshalAs(UnmanagedType.LPWStr)]string s);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, StringBuilder s);
    [DllImport("user32.dll")] public static extern int GetWindowTextLengthW(IntPtr h);
}
"@
Add-Type -TypeDefinition $sig

function GetText([IntPtr]$h) {
    $len = [W]::GetWindowTextLengthW($h)
    $sb = New-Object System.Text.StringBuilder ($len + 8)
    [void][W]::SendMessageW($h, 0x000D, [IntPtr]($len + 8), $sb)   # WM_GETTEXT
    $sb.ToString()
}

$exe = Join-Path $PSScriptRoot "..\dist\sncom.exe"
$p = Start-Process -FilePath $exe -PassThru -WorkingDirectory (Split-Path $exe)
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Milliseconds 300
    $proc = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
    if ($proc -and $proc.MainWindowHandle -ne 0) { $hwnd = $proc.MainWindowHandle; break }
}
if ($hwnd -eq [IntPtr]::Zero) { Write-Output "NO WINDOW"; exit 1 }
Write-Output ("PID=" + $p.Id + " HWND=" + $hwnd)

# switch mode to tcp_client (IDM_MODE_BASE+1 = 2001)
[void][W]::SendMessageW($hwnd, 0x0111, [IntPtr]2001, [IntPtr]::Zero)   # WM_COMMAND
Start-Sleep -Milliseconds 400

$rip   = [W]::GetDlgItem($hwnd, 3064)
$rport = [W]::GetDlgItem($hwnd, 3065)
$nconn = [W]::GetDlgItem($hwnd, 3066)
Write-Output ("rip handle=" + $rip + " rport=" + $rport + " nconn=" + $nconn)
Write-Output ("BEFORE rip=" + (GetText $rip) + " rport=" + (GetText $rport))

[void][W]::SendMessageW($rip,   0x000C, [IntPtr]::Zero, "10.255.255.1")   # WM_SETTEXT (non-routable => SYN_SENT lingers)
[void][W]::SendMessageW($rport, 0x000C, [IntPtr]::Zero, "8080")
Write-Output ("AFTER-SET rip=" + (GetText $rip) + " rport=" + (GetText $rport))

[void][W]::SendMessageW($nconn, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)   # BM_CLICK
Start-Sleep -Milliseconds 600
Write-Output ("AFTER-CLICK rip=" + (GetText $rip) + " rport=" + (GetText $rport))
Start-Sleep -Milliseconds 1500

$ns = netstat -ano | Select-String "$($p.Id)"
Write-Output "NETSTAT:"
$ns | ForEach-Object { Write-Output ("  " + $_.ToString().Trim()) }
Stop-Process -Id $p.Id -Force
