# probe_g21.ps1 - G2-1: send 1MB file over TCP (client -> server on 18080), verify server log
$sig = @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W7 {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, [MarshalAs(UnmanagedType.LPWStr)]string s);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
}
"@
Add-Type -TypeDefinition $sig

function Click([IntPtr]$main, [int]$id) {
    [void][W7]::SendMessageW([W7]::GetDlgItem($main, $id), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
}
function Mode([IntPtr]$main, [int]$cmd) {
    [void][W7]::SendMessageW($main, 0x0111, [IntPtr]$cmd, [IntPtr]::Zero)
}
function StartInst() {
    $p = Start-Process -FilePath $exe -PassThru -WorkingDirectory $dir
    $h = [IntPtr]::Zero
    for ($i=0; $i -lt 30; $i++) { Start-Sleep -Milliseconds 300; $q = Get-Process -Id $p.Id -EA SilentlyContinue; if ($q -and $q.MainWindowHandle -ne 0) { $h = $q.MainWindowHandle; break } }
    @{ P = $p; H = $h }
}

$exe = Join-Path $PSScriptRoot "..\dist\sncom.exe"
$dir = Split-Path $exe

# 1MB patterned file: LINE-000000 .. LINE-065535 (CRLF)
$sendFile = Join-Path $PSScriptRoot "out\sendfile_1m.txt"
$sb = New-Object System.Text.StringBuilder
for ($i = 0; $i -lt 65536; $i++) { [void]$sb.AppendLine(("LINE-" + $i.ToString("D6"))) }
[System.IO.File]::WriteAllText($sendFile, $sb.ToString(), [System.Text.Encoding]::ASCII)
$fsz = (Get-Item $sendFile).Length
Write-Output ("send file: " + $sendFile + " = " + $fsz + " bytes")

Remove-Item (Join-Path $dir "SNCom-Log-*.txt") -ErrorAction SilentlyContinue
@'
{
  "local_port": 18080,
  "remote_ip": "127.0.0.1",
  "remote_port": 18080
}
'@ | Set-Content -Path (Join-Path $dir "sncom.json") -Encoding UTF8

# ---- A: tcp_server 18080 ----
$A = StartInst
Mode $A.H 2002
Start-Sleep -Milliseconds 500
Click $A.H 3039       # rx-to-file on
Click $A.H 3066       # listen
Start-Sleep -Milliseconds 600

# ---- B: tcp_client 18080 ----
$B = StartInst
Mode $B.H 2001
Start-Sleep -Milliseconds 500
Click $B.H 3066       # connect
Start-Sleep -Milliseconds 1200

# ---- B sends the 1MB file ----
[void][W7]::SendMessageW([W7]::GetDlgItem($B.H, 3005), 0x000C, [IntPtr]::Zero, $sendFile)   # file path edit
Click $B.H 3006       # send file (timer9 @1ms, 256B/tick)
Start-Sleep -Milliseconds 15000    # ~4096 ticks, generous
Click $B.H 3007       # stop (no-op if finished)

Stop-Process -Id $A.P.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $B.P.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 800

Write-Output "SERVER LOG CHECK:"
$logs = Get-ChildItem (Join-Path $dir "SNCom-Log-*.txt") | Sort-Object Length -Descending
if (-not $logs) { Write-Output "  NO LOG (server received nothing!)"; exit 1 }
$lg = $logs[0]
$lines = [System.IO.File]::ReadAllLines($lg.FullName)
Write-Output ("  log: " + $lg.Name + " " + $lg.Length + "B, " + $lines.Count + " lines (expect 65536)")
$first = $lines | Select-Object -First 1
$last  = $lines | Select-Object -Last 1
Write-Output ("  first: " + $first.Substring(0, [Math]::Min(70, $first.Length)))
Write-Output ("  last : " + $last.Substring(0, [Math]::Min(70, $last.Length)))
$hitFirst = ($lines | Where-Object { $_ -like "*LINE-000000*" }).Count
$hitLast  = ($lines | Where-Object { $_ -like "*LINE-065535*" }).Count
Write-Output ("  contains LINE-000000: " + $hitFirst + "x, LINE-065535: " + $hitLast + "x")
if ($lines.Count -eq 65536 -and $hitFirst -ge 1 -and $hitLast -ge 1) { Write-Output "G2-1 VERDICT: PASS" } else { Write-Output "G2-1 VERDICT: FAIL" }
