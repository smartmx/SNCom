# probe_duel.ps1 - FINAL two-instance test: server(:8080?) <-> client, send both ways, check logs
# ES_NUMBER edits reject WM_SETTEXT -> type via WM_CHAR instead.
$sig = @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W5 {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, [MarshalAs(UnmanagedType.LPWStr)]string s);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
}
"@
Add-Type -TypeDefinition $sig

function DlgText([IntPtr]$main, [int]$id) {
    $sb = New-Object System.Text.StringBuilder 256
    [void][W5]::GetWindowTextW([W5]::GetDlgItem($main, $id), $sb, 256)
    $sb.ToString()
}
function TypeInto([IntPtr]$main, [int]$id, [string]$s) {
    $h = [W5]::GetDlgItem($main, $id)
    [void][W5]::SendMessageW($h, 0x000C, [IntPtr]::Zero, "")    # clear (WM_SETTEXT empty)
    foreach ($ch in $s.ToCharArray()) {
        [void][W5]::SendMessageW($h, 0x0102, [IntPtr][int]$ch, [IntPtr]::Zero)   # WM_CHAR
    }
}
function Click([IntPtr]$main, [int]$id) {
    [void][W5]::SendMessageW([W5]::GetDlgItem($main, $id), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)   # BM_CLICK
}
function Mode([IntPtr]$main, [int]$cmd) {
    [void][W5]::SendMessageW($main, 0x0111, [IntPtr]$cmd, [IntPtr]::Zero)   # WM_COMMAND
}

$exe = Join-Path $PSScriptRoot "..\dist\sncom.exe"
$dir = Split-Path $exe
Remove-Item (Join-Path $dir "SNCom-Log-*.txt") -ErrorAction SilentlyContinue

function StartInst() {
    $p = Start-Process -FilePath $exe -PassThru -WorkingDirectory $dir
    $h = [IntPtr]::Zero
    for ($i=0; $i -lt 30; $i++) { Start-Sleep -Milliseconds 300; $q = Get-Process -Id $p.Id -EA SilentlyContinue; if ($q -and $q.MainWindowHandle -ne 0) { $h = $q.MainWindowHandle; break } }
    @{ P = $p; H = $h }
}

# ---- A: tcp_server, local port 8080 ----
$A = StartInst
Mode $A.H 2002
Start-Sleep -Milliseconds 600
TypeInto $A.H 3073 "8080"
Write-Output ("A lport after typing: " + (DlgText $A.H 3073))
Click $A.H 3039       # rx-to-file on
Click $A.H 3066       # listen
Start-Sleep -Milliseconds 600
Write-Output "A netstat:"
netstat -ano | Select-String "\s$($A.P.Id)\s*$" | ForEach-Object { Write-Output ("  " + $_.ToString().Trim()) }

# ---- B: tcp_client 127.0.0.1:8080 ----
$B = StartInst
Mode $B.H 2001
Start-Sleep -Milliseconds 600
TypeInto $B.H 3064 "127.0.0.1"
TypeInto $B.H 3065 "8080"
Write-Output ("B rip=" + (DlgText $B.H 3064) + " rport=" + (DlgText $B.H 3065))
Click $B.H 3039       # rx-to-file on
Click $B.H 3066       # connect
Start-Sleep -Milliseconds 1500
Write-Output "B netstat:"
netstat -ano | Select-String "\s$($B.P.Id)\s*$" | ForEach-Object { Write-Output ("  " + $_.ToString().Trim()) }

# ---- B sends HELLO-8080 ----
[void][W5]::SendMessageW([W5]::GetDlgItem($B.H, 3057), 0x000C, [IntPtr]::Zero, "HELLO-8080")
Click $B.H 3058
Start-Sleep -Milliseconds 900

# ---- A broadcasts WORLD-BACK ----
[void][W5]::SendMessageW([W5]::GetDlgItem($A.H, 3057), 0x000C, [IntPtr]::Zero, "WORLD-BACK")
Click $A.H 3058
Start-Sleep -Milliseconds 900

Write-Output "A netstat after exchanges:"
netstat -ano | Select-String "\s$($A.P.Id)\s*$" | ForEach-Object { Write-Output ("  " + $_.ToString().Trim()) }

Write-Output "LOGS:"
Get-ChildItem (Join-Path $dir "SNCom-Log-*.txt") | ForEach-Object {
    Write-Output ("  === " + $_.Name + " (" + $_.Length + "B) ===")
    Get-Content $_.FullName -Encoding UTF8 | Select-Object -First 8 | ForEach-Object { Write-Output ("    " + $_) }
}
Stop-Process -Id $A.P.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $B.P.Id -Force -ErrorAction SilentlyContinue
