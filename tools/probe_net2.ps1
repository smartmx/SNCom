# probe_net2.ps1 - two-instance interop test: server(listen 8080) <-> client(connect 8080)
$sig = @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W2 {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr Send(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] public static extern IntPtr SendStr(IntPtr h, uint m, IntPtr w, string s);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
}
"@
Add-Type -TypeDefinition $sig

function GetDlgItemText([IntPtr]$dlg, [int]$id) {
    $h = [W2]::GetDlgItem($dlg, $id)
    $sb = New-Object System.Text.StringBuilder 256
    [void][W2]::GetWindowTextW($h, $sb, 256)
    $sb.ToString()
}
function StatusBarPart([IntPtr]$main, [int]$part) {
    $sb = [W2]::FindWindowExW($main, [IntPtr]::Zero, "msctls_statusbar32", $null)
    if ($sb -eq [IntPtr]::Zero) { return "(no statusbar)" }
    $ptr = [System.Runtime.InteropServices.Marshal]::AllocHGlobal(512)
    [void][W2]::Send($sb, 0x0410, [IntPtr]$part, $ptr)        # SB_GETTEXTW
    $s = [System.Runtime.InteropServices.Marshal]::PtrToStringUni($ptr)
    [System.Runtime.InteropServices.Marshal]::FreeHGlobal($ptr)
    $s
}
function StatusAll([IntPtr]$main) {
    (0..4 | ForEach-Object { StatusBarPart $main $_ }) -join " | "
}

$exe = Join-Path $PSScriptRoot "..\dist\sncom.exe"
$dir = Split-Path $exe
Remove-Item (Join-Path $dir "SNCom-Log-*.txt") -ErrorAction SilentlyContinue

# ---- instance A: TCP server, listen 127.0.0.1:8080 ----
$pa = Start-Process -FilePath $exe -PassThru -WorkingDirectory $dir
$hA = [IntPtr]::Zero
for ($i=0; $i -lt 30; $i++) { Start-Sleep -Milliseconds 300; $p = Get-Process -Id $pa.Id -EA SilentlyContinue; if ($p -and $p.MainWindowHandle -ne 0) { $hA = $p.MainWindowHandle; break } }
[void][W2]::Send($hA, 0x0111, [IntPtr]2002, [IntPtr]::Zero)                # mode = tcp_server
Start-Sleep -Milliseconds 300
[void][W2]::SendStr([W2]::GetDlgItem($hA, 3073), 0x000C, [IntPtr]::Zero, "8080")   # local port
Write-Output ("A localport-box=" + (GetDlgItemText $hA 3073))
[void][W2]::Send([W2]::GetDlgItem($hA, 3039), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)  # rx-to-file on
[void][W2]::Send([W2]::GetDlgItem($hA, 3066), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)  # listen
Start-Sleep -Milliseconds 500
Write-Output ("A status after listen: " + (StatusAll $hA))

# ---- instance B: TCP client -> 127.0.0.1:8080 ----
$pb = Start-Process -FilePath $exe -PassThru -WorkingDirectory $dir
$hB = [IntPtr]::Zero
for ($i=0; $i -lt 30; $i++) { Start-Sleep -Milliseconds 300; $p = Get-Process -Id $pb.Id -EA SilentlyContinue; if ($p -and $p.MainWindowHandle -ne 0) { $hB = $p.MainWindowHandle; break } }
[void][W2]::Send($hB, 0x0111, [IntPtr]2001, [IntPtr]::Zero)                # mode = tcp_client
Start-Sleep -Milliseconds 300
[void][W2]::SendStr([W2]::GetDlgItem($hB, 3064), 0x000C, [IntPtr]::Zero, "127.0.0.1")  # remote ip
[void][W2]::SendStr([W2]::GetDlgItem($hB, 3065), 0x000C, [IntPtr]::Zero, "8080")       # remote port
Write-Output ("B remoteport-box=" + (GetDlgItemText $hB 3065))
[void][W2]::Send([W2]::GetDlgItem($hB, 3039), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)  # rx-to-file on
[void][W2]::Send([W2]::GetDlgItem($hB, 3066), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)  # connect
Start-Sleep -Milliseconds 1200
Write-Output ("B status after connect: " + (StatusAll $hB))
Write-Output ("A status after accept : " + (StatusAll $hA))

# ---- B sends "HELLO" ----
[void][W2]::SendStr([W2]::GetDlgItem($hB, 3057), 0x000C, [IntPtr]::Zero, "HELLO-8080")  # send edit
[void][W2]::Send([W2]::GetDlgItem($hB, 3058), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)   # btn send
Start-Sleep -Milliseconds 800
Write-Output ("B status after send: " + (StatusAll $hB))
Write-Output ("A status after recv : " + (StatusAll $hA))

# ---- A (server) sends "WORLD" (broadcast) ----
[void][W2]::SendStr([W2]::GetDlgItem($hA, 3057), 0x000C, [IntPtr]::Zero, "WORLD-BACK")
[void][W2]::Send([W2]::GetDlgItem($hA, 3058), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Milliseconds 800
Write-Output ("A status after srvsend: " + (StatusAll $hA))
Write-Output ("B status after srvrecv: " + (StatusAll $hB))

Write-Output "LOG FILES:"
Get-ChildItem (Join-Path $dir "SNCom-Log-*.txt") | ForEach-Object {
    Write-Output ("  " + $_.Name + " " + $_.Length + "B")
    Get-Content $_.FullName -Encoding UTF8 -TotalCount 6 | ForEach-Object { Write-Output ("    " + $_) }
}
Stop-Process -Id $pa.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $pb.Id -Force -ErrorAction SilentlyContinue
