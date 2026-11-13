<#
.SYNOPSIS
  Real OBS GUI smoke test in an ISOLATED portable copy of the installed OBS Studio
  (your own OBS profile, scenes and plugins folder are never touched).

  1. copies the installed OBS into -WorkDir and enables portable mode
  2. installs build\stage\vmedia-ai-autoframe in the portable plugin layout
     (obs-plugins\64bit + data\obs-plugins\<name>, relative data path - like a real install)
  3. writes a scene collection: Media Source "Camera" (tests\media\23117.mp4) + AutoFrame filter
  4. launches obs64.exe, waits, checks the OBS log for: plugin loaded, detector ready, TRACKING
  5. closes OBS normally and checks: no crash report, "Number of memory leaks: 0"
#>
param(
    [string]$WorkDir = (Join-Path $env:TEMP 'vmaf-obs-gui-test'),
    [int]$Seconds = 25
)
$ErrorActionPreference = 'Stop'
$Proj = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $Proj 'build\stage\vmedia-ai-autoframe'
$clip = Join-Path $Proj 'tests\media\23117.mp4'
if (-not (Test-Path "$stage\bin\64bit\vmedia-ai-autoframe.dll")) { throw 'build first (scripts\build.ps1)' }
if (-not (Test-Path $clip)) { throw 'run tests\fetch-test-media.ps1 first' }
$obsSrc = (Get-ItemProperty 'HKLM:\SOFTWARE\OBS Studio' -ErrorAction SilentlyContinue).'(default)'
if (-not $obsSrc) { $obsSrc = Join-Path $env:ProgramFiles 'obs-studio' }

if (Test-Path $WorkDir) { Remove-Item -Recurse -Force $WorkDir }
robocopy $obsSrc $WorkDir /E /NFL /NDL /NJH /NJS /NP | Out-Null
New-Item -ItemType Directory -Force "$WorkDir\obs-plugins\64bit", "$WorkDir\data\obs-plugins\vmedia-ai-autoframe" | Out-Null
Copy-Item "$stage\bin\64bit\*" "$WorkDir\obs-plugins\64bit\" -Force
Copy-Item -Recurse -Force "$stage\data\*" "$WorkDir\data\obs-plugins\vmedia-ai-autoframe\"

$cfg = "$WorkDir\config\obs-studio"
New-Item -ItemType Directory -Force "$cfg\basic\scenes", "$cfg\basic\profiles\Test" | Out-Null
# OBS treats a config without LastVersion as a first run and opens the Auto-Configuration
# Wizard (a modal that blocks closing). LastVersion = MAKE_SEMANTIC_VERSION(major, minor, patch).
$v = (Get-Item "$WorkDir\bin\64bit\obs64.exe").VersionInfo
$lastVersion = ($v.FileMajorPart -shl 24) -bor ($v.FileMinorPart -shl 16) -bor $v.FileBuildPart
$ini = "[General]`r`nFirstRun=false`r`nLastVersion=$lastVersion`r`nEnableAutoUpdates=false`r`n[Basic]`r`nProfile=Test`r`nProfileDir=Test`r`nSceneCollection=AutoFrameTest`r`nSceneCollectionFile=AutoFrameTest`r`n"
Set-Content "$cfg\user.ini" $ini -Encoding ascii
Set-Content "$cfg\global.ini" $ini -Encoding ascii
"[General]`r`nName=Test`r`n[Video]`r`nBaseCX=1920`r`nBaseCY=1080`r`nOutputCX=1920`r`nOutputCY=1080`r`nFPSCommon=30`r`n" |
    Set-Content "$cfg\basic\profiles\Test\basic.ini" -Encoding ascii
$clipJson = $clip -replace '\\', '/'
@"
{ "name": "AutoFrameTest", "current_scene": "Main", "current_program_scene": "Main", "scene_order": [ { "name": "Main" } ],
  "sources": [
    { "id": "scene", "versioned_id": "scene", "name": "Main", "uuid": "11111111-1111-4111-8111-111111111111",
      "settings": { "id_counter": 1, "items": [ { "name": "Camera", "source_uuid": "22222222-2222-4222-8222-222222222222",
        "visible": true, "id": 1, "pos": { "x": 0.0, "y": 0.0 }, "scale": { "x": 1.5, "y": 1.5 }, "rot": 0.0, "alignment": 5, "bounds_type": 0 } ] } },
    { "id": "ffmpeg_source", "versioned_id": "ffmpeg_source", "name": "Camera", "uuid": "22222222-2222-4222-8222-222222222222",
      "settings": { "local_file": "$clipJson", "is_local_file": true, "looping": true, "hw_decode": false },
      "filters": [ { "id": "vmedia_ai_autoframe_filter", "versioned_id": "vmedia_ai_autoframe_filter", "name": "V Media AI AutoFrame",
                     "settings": { "show_boxes": true, "show_stats": true, "diagnostic_logs": true } } ] } ] }
"@ | Set-Content "$cfg\basic\scenes\AutoFrameTest.json" -Encoding utf8

$t0 = Get-Date
$obs = Start-Process -FilePath "$WorkDir\bin\64bit\obs64.exe" -WorkingDirectory "$WorkDir\bin\64bit" -PassThru `
    -ArgumentList '--portable', '--disable-updater', '--disable-shutdown-check', '--collection', 'AutoFrameTest', '--profile', 'Test'
Start-Sleep -Seconds $Seconds
$obs.Refresh()
$alive = -not $obs.HasExited
if ($alive) { $obs.CloseMainWindow() | Out-Null; $obs.WaitForExit(20000) | Out-Null }
$log = Get-ChildItem "$cfg\logs" | Where-Object LastWriteTime -gt $t0 | Sort-Object LastWriteTime | Select-Object -Last 1
$text = if ($log) { Get-Content $log.FullName -Raw } else { '' }
$crashes = @(Get-ChildItem "$cfg\crashes" -ErrorAction SilentlyContinue).Count

$checks = [ordered]@{
    'OBS still running after start'        = $alive
    'plugin loaded by the OBS frontend'     = $text -match 'V Media AI AutoFrame [\d.]+ loaded'
    'filter restored from scene collection' = $text -match "filter: 'V Media AI AutoFrame'"
    'AI detector ready'                     = $text -match 'detector ready'
    'tracking a person'                     = $text -match "TRACKING target #\d+"
    'no crash report'                       = $crashes -eq 0
    'OBS exited normally'                   = $obs.HasExited
    'Number of memory leaks: 0'             = $text -match 'Number of memory leaks: 0'
}
$fail = 0
foreach ($k in $checks.Keys) {
    if ($checks[$k]) { Write-Host "  ok:   $k" } else { Write-Host "  FAIL: $k" -ForegroundColor Red; $fail++ }
}
($text -split "`n") | Where-Object { $_ -match 'vmedia-autoframe' } | Select-Object -First 6 | ForEach-Object { "    $_" }
if ($fail) { throw "GUI smoke test failed ($fail)" }
Write-Host 'OBS GUI SMOKE TEST PASSED' -ForegroundColor Green
