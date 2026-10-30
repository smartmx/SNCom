# probe_duel2.ps1 - interop test on FREE port 18080, ports injected via sncom.json (no typing)
$sig = @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W6 {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, [MarshalAs(UnmanagedType.LPWStr)]string s);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
}
"@
Add-Type -TypeDefinition $sig

function DlgText([IntPtr]$main, [int]$id) {
    $sb = New-Object System.Text.StringBuilder 256
    [void][W6]::GetWindowTextW([W6]::GetDlgItem($main, $id), $sb, 256)
    $sb.ToString()
}
function Click([IntPtr]$main, [int]$id) {
    [void][W6]::SendMessageW([W6]::GetDlgItem($main, $id), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
}
function Mode([IntPtr]$main, [int]$cmd) {
    [void][W6]::SendMessageW($main, 0x0111, [IntPtr]$cmd, [IntPtr]::Zero)
}

$exe  = Join-Path $PSScriptRoot "..\dist\sncom.exe"
$dir  = Split-Path $exe
$json = Join-Path $dir "sncom.json"
Remove-Item (Join-Path $dir "SNCom-Log-*.txt") -ErrorAction SilentlyContinue

# ports via config (loader reads local_port / remote_ip / remote_port)
@'
{
  "local_port": 18080,
  "remote_ip": "127.0.0.1",
  "remote_port": 18080
}
'@ | Set-Content -Path $json -Encoding UTF8

function StartInst() {
    $p = Start-Process -FilePath $exe -PassThru -WorkingDirectory $dir
    $h = [IntPtr]::Zero
    for ($i=0; $i -lt 30; $i++) { Start-Sleep -Milliseconds 300; $q = Get-Process -Id $p.Id -EA SilentlyContinue; if ($q -and $q.MainWindowHandle -ne 0) { $h = $q.MainWindowHandle; break } }
    @{ P = $p; H = $h }
}

# ---- A: tcp_server on 18080 ----
$A = StartInst
Mode $A.H 2002
Start-Sleep -Milliseconds 600
Write-Output ("A netmode label: " + (DlgText $A.H 3062) + "  lport box: " + (DlgText $A.H 3073))
Click $A.H 3039       # rx-to-file
Click $A.H 3066       # listen
Start-Sleep -Milliseconds 800
Write-Output "A netstat (expect LISTENING :18080):"
netstat -ano | Select-String "\s$($A.P.Id)\s*$" | ForEach-Object { Write-Output ("  " + $_.ToString().Trim()) }

# ---- B: tcp_client -> 127.0.0.1:18080 ----
$B = StartInst
Mode $B.H 2001
Start-Sleep -Milliseconds 600
Write-Output ("B netmode label: " + (DlgText $B.H 3062) + "  rip/rport: " + (DlgText $B.H 3064) + ":" + (DlgText $B.H 3065))
Click $B.H 3039       # rx-to-file
Click $B.H 3066       # connect
Start-Sleep -Milliseconds 1500
Write-Output "B netstat (expect ESTABLISHED :18080):"
netstat -ano | Select-String "\s$($B.P.Id)\s*$" | ForEach-Object { Write-Output ("  " + $_.ToString().Trim()) }

# ---- B sends HELLO-18080 ----
[void][W6]::SendMessageW([W6]::GetDlgItem($B.H, 3057), 0x000C, [IntPtr]::Zero, "HELLO-18080")
Click $B.H 3058
Start-Sleep -Milliseconds 900

# ---- A broadcasts WORLD-BACK ----
[void][W6]::SendMessageW([W6]::GetDlgItem($A.H, 3057), 0x000C, [IntPtr]::Zero, "WORLD-BACK")
Click $A.H 3058
Start-Sleep -Milliseconds 900

Stop-Process -Id $A.P.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $B.P.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 800

Write-Output "LOGS (A=server first by time, B=client):"
Get-ChildItem (Join-Path $dir "SNCom-Log-*.txt") | ForEach-Object {
    Write-Output ("  === " + $_.Name + " (" + $_.Length + "B) ===")
    Get-Content $_.FullName -Encoding UTF8 | Select-Object -First 10 | ForEach-Object { Write-Output ("    " + $_) }
}
