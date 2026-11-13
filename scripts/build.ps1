<#
.SYNOPSIS
  Builds V Media AI AutoFrame (Release) and runs the automated tests.

.PARAMETER Config        Release (default) or Debug / RelWithDebInfo
.PARAMETER SkipTests     Build only
.PARAMETER Integration   Also run the libobs integration test (needs an installed OBS Studio
                         33.x and tests\fetch-test-media.ps1 media)
#>
[CmdletBinding()]
param(
    [string]$Config = 'Release',
    [switch]$SkipTests,
    [switch]$Integration
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $cm = 'C:\Program Files\CMake\bin'
    if (Test-Path "$cm\cmake.exe") { $env:Path = "$cm;$env:Path" } else { throw 'CMake 3.24+ not found' }
}
if (-not (Test-Path 'third_party\obs-sdk\lib\obs.lib')) { & "$PSScriptRoot\setup.ps1" }

cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE) { throw 'CMake configure failed' }
cmake --build build --config $Config --parallel
if ($LASTEXITCODE) { throw 'Build failed' }
Write-Host "`nPlugin staged in build\stage\vmedia-ai-autoframe" -ForegroundColor Green

if ($SkipTests) { return }
Write-Host "`n== Unit tests" -ForegroundColor Cyan
& "build\$Config\vmaf-tests.exe"
if ($LASTEXITCODE) { throw 'Unit tests failed' }

Write-Host "`n== Detector test (real ONNX Runtime + models)" -ForegroundColor Cyan
& "build\$Config\vmaf-detector-test.exe"
if ($LASTEXITCODE) { throw 'Detector test failed' }

if ($Integration) {
    Write-Host "`n== libobs integration test" -ForegroundColor Cyan
    $obsBin = Join-Path $env:ProgramFiles 'obs-studio\bin\64bit'
    if (-not (Test-Path 'tests\media\23117.mp4')) { & "$Root\tests\fetch-test-media.ps1" }
    $args5 = @($obsBin, "$Root\build\stage\vmedia-ai-autoframe", "$Root\tests\media", "$Root\tests\out")
    & "build\$Config\obs-headless-test.exe" @args5
    if ($LASTEXITCODE) { throw 'Integration test failed' }
    Write-Host "`n== libobs 1080p30 recording test (x264 + AI)" -ForegroundColor Cyan
    & "build\$Config\obs-headless-test.exe" @args5 record
    if ($LASTEXITCODE) { throw 'Recording test failed' }
    Write-Host "`n== libobs vertical 1080x1920 canvas test (9:16 + encode)" -ForegroundColor Cyan
    & "build\$Config\obs-headless-test.exe" @args5 vertical
    if ($LASTEXITCODE) { throw 'Vertical test failed' }
    Write-Host "`n== libobs horizontal + vertical from one camera (filters on scenes)" -ForegroundColor Cyan
    & "build\$Config\obs-headless-test.exe" @args5 scenefilter
    if ($LASTEXITCODE) { throw 'Scene-filter test failed' }
    Write-Host "`n== libobs test: ONNX Runtime missing (fresh process, must not crash)" -ForegroundColor Cyan
    & "build\$Config\obs-headless-test.exe" @args5 noruntime
    if ($LASTEXITCODE) { throw 'No-runtime test failed' }
    Write-Host "`n== Real OBS GUI smoke test (isolated portable copy)" -ForegroundColor Cyan
    & "$Root\tests\gui-smoke-test.ps1"
}
Write-Host "`nAll requested tests passed." -ForegroundColor Green
