# Serialized build + autotest run for parallel agents.
# Usage: powershell -ExecutionPolicy Bypass -File Tools\PortraitTest.ps1 -Tag hud [-W 450 -H 1000] [-NoBuild] [-Landscape] [-Both]
# Screenshots: Saved\Shots_<Tag>_<W>x<H>\NN_name.png ; log summary printed at the end.
# -Both (update 6, spec §14 "both orientations"): builds once, runs the portrait test, then the landscape one (tag <Tag>_land).
param([string]$Tag = "agent", [int]$W = 450, [int]$H = 1000, [switch]$NoBuild, [switch]$Landscape, [switch]$Both)
$ErrorActionPreference = "Continue"
if ($Both) {
    & $PSCommandPath -Tag $Tag -W $W -H $H -NoBuild:$NoBuild
    & $PSCommandPath -Tag "${Tag}_land" -Landscape -NoBuild
    return
}
# Paths come from Tools\Env.bat (the only machine-specific file); calling it also creates the ASCII junction if needed.
$envVars = @{}
cmd /c "`"$PSScriptRoot\Env.bat`" & set" | ForEach-Object { if ($_ -match '^(\w+)=(.*)$') { $envVars[$matches[1]] = $matches[2] } }
$proj = $envVars.FV_PROJ
$editor = $envVars.UE_EDITOR
if ($Landscape) { $W = 1280; $H = 720 }
$lock = "$proj\Saved\test.lock"
New-Item -ItemType Directory -Force "$proj\Saved" | Out-Null
$waited = 0
while ($true) {
    try { New-Item -ItemType Directory $lock -ErrorAction Stop | Out-Null; break }
    catch {
        $age = ((Get-Date) - (Get-Item $lock).CreationTime).TotalMinutes
        if ($age -gt 10) { Remove-Item $lock -Recurse -Force -ErrorAction SilentlyContinue; continue }
        if ($waited % 60 -eq 0) { "waiting for another test run to finish..." }
        Start-Sleep -Seconds 5; $waited += 5
    }
}
try {
    if (-not $NoBuild) {
        $out = cmd /c "$proj\Tools\BuildEditor.bat" 2>&1
        # (skip UBT's note about the banned VS2022 MSVC 14.41 toolchain: it is unused, 14.51 builds the project)
        $errs = $out | Select-String -Pattern "error|Result:" | Where-Object { $_.Line -notmatch "banned the MSVC" }
        $errs | Select-Object -First 40 | ForEach-Object { $_.Line }
        if (-not ($out | Select-String -Pattern "Result: Succeeded" -Quiet)) { "BUILD FAILED - not running the game"; return }
    }
    $shotDir = "Shots_${Tag}_${W}x${H}"
    if (Test-Path "$proj\Saved\$shotDir") { Get-ChildItem "$proj\Saved\$shotDir" -Filter *.png | ForEach-Object { Remove-Item -LiteralPath $_.FullName } }
    $logName = "FV_test_$Tag.log"
    $log = "$proj\Saved\Logs\$logName"
    if (Test-Path $log) { Clear-Content $log }
    $args = '"' + $proj + '\FishingValley.uproject" -game -windowed -ResX=' + $W + ' -ResY=' + $H + ' -WinX=20 -WinY=20 -fvautotest -fvtouch -fvshotdir=' + $shotDir + ' -log=' + $logName + ' -nosplash -unattended'
    $p = Start-Process -FilePath $editor -ArgumentList $args -PassThru
    if (-not $p.WaitForExit(480000)) { Stop-Process -Id $p.Id -Force; "GAME KILLED (timeout)" }
    Select-String -Path $log -Pattern "FV: AUTOTEST (touch|WARNING|DONE|TIMEOUT|u6 summary|legends summary)|Error|Fatal|Ensure|Assert|Unhandled" | ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[[^\]]+\]', '' } | Select-Object -First 30
    "screenshots in $proj\Saved\$shotDir :"
    Get-ChildItem "$proj\Saved\$shotDir" -Filter *.png | ForEach-Object { $_.Name }
}
finally { Remove-Item $lock -Recurse -Force -ErrorAction SilentlyContinue }
