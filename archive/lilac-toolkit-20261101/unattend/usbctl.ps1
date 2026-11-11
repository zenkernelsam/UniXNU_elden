# usbctl.ps1 — elevated USB control daemon for Xperia automation
# Runs all night as SYSTEM scheduled task "XperiaUsbCtl".
# Commands (*.cmd files in .\cmd\): replug | hardreplug | adbreset | status | stop
# Results -> .\out\result.txt (UTF-16); activity -> .\out\daemon.log

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$cmdDir = Join-Path $root 'cmd'
$outDir = Join-Path $root 'out'
New-Item -ItemType Directory -Force -Path $cmdDir, $outDir | Out-Null
$adb = 'D:\Desktop\Git\SchoolBox\Tools\platform-tools_ADB\adb.exe'

function Log($m) { "$(Get-Date -Format o)  $m" | Out-File "$outDir\daemon.log" -Append }
function Phone-Devs { Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match 'VID_0FCE' } }

# Level 1: pnputil restart-device (official reset path, tolerates in-use devices)
function Do-RestartDev {
    $lines = @()
    foreach ($d in (Phone-Devs)) {
        $o = & pnputil /restart-device "$($d.InstanceId)" 2>&1 | Out-String
        $lines += "restart $($d.InstanceId): $(($o -split "`n" | Where-Object {$_ -match '\S'})[-1].Trim())"
    }
    if (-not $lines) { return 'no phone device present' }
    return ($lines -join "`n")
}

# Level 2: disable/enable cycle with retries
function Do-DisEn {
    $lines = @()
    foreach ($d in (Phone-Devs)) {
        $ok = $false
        for ($t = 0; $t -lt 3 -and -not $ok; $t++) {
            try { Disable-PnpDevice -InstanceId $d.InstanceId -Confirm:$false -ErrorAction Stop
                  $ok = $true; $lines += "dis  $($d.InstanceId)" }
            catch { $lines += "disFAIL$t $($d.InstanceId) $($_.Exception.Message)"; Start-Sleep 1 }
        }
    }
    Start-Sleep -Seconds 3
    foreach ($d in (Phone-Devs)) {
        try { Enable-PnpDevice -InstanceId $d.InstanceId -Confirm:$false -ErrorAction Stop
              $lines += "en   $($d.InstanceId)" }
        catch { $lines += "enFAIL $($d.InstanceId) $($_.Exception.Message)" }
    }
    return ($lines -join "`n")
}

# Level 3: power-cycle the parent USB hub/port (closest to physical replug).
# WARNING: may bounce siblings on the same hub — used only on hardreplug.
function Do-HubCycle {
    $d = Phone-Devs | Select-Object -First 1
    if (-not $d) { return 'no phone device present' }
    $parent = (Get-PnpDeviceProperty -InstanceId $d.InstanceId -KeyName 'DEVPKEY_Device_Parent' -ErrorAction SilentlyContinue).Data
    if (-not $parent) { return 'no parent found' }
    try {
        Disable-PnpDevice -InstanceId $parent -Confirm:$false -ErrorAction Stop
        Start-Sleep -Seconds 3
        Enable-PnpDevice -InstanceId $parent -Confirm:$false -ErrorAction Stop
        return "hub cycled: $parent"
    } catch { return "hub FAIL $parent $($_.Exception.Message)" }
}

function Do-AdbReset {
    & $adb kill-server 2>&1 | Out-Null
    Start-Sleep -Seconds 2
    & $adb start-server 2>&1 | Out-Null
    Start-Sleep -Seconds 4
    return (& $adb devices | Out-String).Trim()
}

# Level 4: uninstall device node + rescan = brand-new enumeration, clears the
# "needs reboot to finish configuration" pending flag that blocks dis/en.
function Dev-Reset {
    $lines = @()
    foreach ($d in (Phone-Devs)) {
        $o = & pnputil /remove-device "$($d.InstanceId)" 2>&1 | Out-String
        $lines += "rm $($d.InstanceId): $(($o -split "`n" | Where-Object {$_ -match '\S'})[-1].Trim())"
    }
    Start-Sleep -Seconds 2
    $o = & pnputil /scan-devices 2>&1 | Out-String
    $lines += "scan: $(($o -split "`n" | Where-Object {$_ -match '\S'})[-1].Trim())"
    return ($lines -join "`n")
}

function Do-Status {
    $d = Phone-Devs | ForEach-Object { "$($_.Status) $($_.Class) $($_.FriendlyName) $($_.InstanceId)" }
    $a = (& $adb devices | Out-String).Trim()
    $f = (& 'D:\Desktop\Git\SchoolBox\Tools\platform-tools_ADB\fastboot.exe' devices 2>&1 | Out-String).Trim()
    return "DEVS:`n$($d -join "`n")`nADB:`n$a`nFASTBOOT:`n$f"
}

Log "daemon started pid=$PID"
$script:run = $true
while ($script:run) {
    Get-ChildItem "$cmdDir\*.cmd" -ErrorAction SilentlyContinue | ForEach-Object {
        $raw = (Get-Content $_.FullName -Raw).Trim()
        Remove-Item $_.FullName -Force
        Log "cmd: $raw"
        if ($raw.StartsWith('ps:')) {
            $result = try { Invoke-Expression $raw.Substring(3) 2>&1 | Out-String } catch { "ERR: $($_.Exception.Message)" }
        } else {
            $cmd = $raw.ToLower()
            $result = switch -Regex ($cmd) {
            '^replug$'     { Do-RestartDev; Do-DisEn }
            '^hardreplug$' { Do-DisEn; Do-HubCycle }
            '^adbreset$'   { Do-AdbReset }
            '^status$'     { Do-Status }
            '^devreset$'   { Dev-Reset }
            '^stop$'       { $script:run = $false; 'daemon stopping' }
            default        { "unknown cmd: $cmd" }
            }
        }
        "$(Get-Date -Format o)`n$result" | Out-File "$outDir\result.txt"
        Log "done: $raw"
    }
    Start-Sleep -Seconds 2
}
Log "daemon stopped"

