# Exports the game art for the web site and the admin panel (update 6), then builds the site images.
#   powershell -ExecutionPolicy Bypass -File Tools\ExportArt.ps1 [-NoGif] [-NoBuild] [-SkipGame]
# 1. runs the game with -fvexportart (FVIconExport.cpp): icons x4 (Saved\ArtExport\icons, manifest.json) and, unless
#    -NoGif, one real fishing session at the pond captured as frames (Saved\ArtExport_gif\gif_NNNN.png);
#    the game uses the autotest save slot (backed up and restored here) and never talks to the server;
# 2. python Tools\build_site_art.py copies the icons to Server\Fishing-game\img\icons and builds the gameplay
#    GIF / WebP, the screenshot gallery and the location cards (img\shots, img\loc, img\art.json).
# The game window must stay visible (UI screenshots of a minimized window are skipped). No system input is emulated.
param([switch]$NoGif, [switch]$NoBuild, [switch]$SkipGame, [int]$W = 450, [int]$H = 1000)
$ErrorActionPreference = "Continue"
$envVars = @{}
cmd /c "`"$PSScriptRoot\Env.bat`" & set" | ForEach-Object { if ($_ -match '^(\w+)=(.*)$') { $envVars[$matches[1]] = $matches[2] } }
$proj = $envVars.FV_PROJ
$editor = $envVars.UE_EDITOR
if (-not $SkipGame) {
    $lock = "$proj\Saved\test.lock"   # the same lock as PortraitTest.ps1 (the autotest save slot is shared)
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
    $slot = "$proj\Saved\SaveGames\FishingValley_AutoTest.sav"
    $backup = "$proj\Saved\SaveGames\FishingValley_AutoTest.sav.exportbak"
    try {
        if (-not $NoBuild) {
            $out = cmd /c "$proj\Tools\BuildEditor.bat" 2>&1
            $out | Select-String -Pattern "error C|error LNK|Result:" | Select-Object -First 30 | ForEach-Object { $_.Line }
            if (-not ($out | Select-String -Pattern "Result: Succeeded" -Quiet)) { "BUILD FAILED - not running the game"; return }
        }
        if (Test-Path $slot) { Copy-Item $slot $backup -Force }
        foreach ($d in @("$proj\Saved\ArtExport", "$proj\Saved\ArtExport_gif")) { if (Test-Path $d) { Remove-Item $d -Recurse -Force } }
        $log = "$proj\Saved\Logs\FV_exportart.log"
        if (Test-Path $log) { Clear-Content $log }
        $gif = if ($NoGif) { "" } else { " -fvexportgif -fvshotdir=ArtExport_gif" }
        $args = '"' + $proj + '\FishingValley.uproject" -game -windowed -ResX=' + $W + ' -ResY=' + $H + ' -WinX=20 -WinY=20 -fvtouch -fvserver=off -fvexportart=ArtExport' + $gif + ' -log=FV_exportart.log -nosplash -unattended'
        $p = Start-Process -FilePath $editor -ArgumentList $args -PassThru
        if (-not $p.WaitForExit(300000)) { Stop-Process -Id $p.Id -Force; "GAME KILLED (timeout)" }
        Select-String -Path $log -Pattern "EXPORTART|Error|Fatal|Unhandled" | ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[[^\]]+\]', '' } | Select-Object -First 30
    }
    finally {
        if (Test-Path $backup) { Move-Item $backup $slot -Force }
        Remove-Item $lock -Recurse -Force -ErrorAction SilentlyContinue
    }
}
python "$PSScriptRoot\build_site_art.py"
