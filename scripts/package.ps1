<#
.SYNOPSIS
  Creates the release files in release\:
    VMedia-AI-AutoFrame-Setup-x64.exe   Inno Setup installer
    VMedia-AI-AutoFrame-<ver>-x64.zip   manual install (extract into C:\ProgramData\obs-studio\plugins)
    SHA256SUMS.txt                      checksums of both
  Run scripts\build.ps1 first.
#>
[CmdletBinding()]
param([string]$Config = 'Release')
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$stage = Join-Path $Root 'build\stage\vmedia-ai-autoframe'
if (-not (Test-Path "$stage\bin\64bit\vmedia-ai-autoframe.dll")) { throw 'Nothing staged - run scripts\build.ps1 first.' }
$version = (Select-String -Path CMakeLists.txt -Pattern 'VERSION (\d+\.\d+\.\d+)' | Select-Object -First 1).Matches[0].Groups[1].Value
$rel = Join-Path $Root 'release'
if (Test-Path $rel) { Remove-Item -Recurse -Force $rel }
New-Item -ItemType Directory -Force -Path $rel | Out-Null

# Verify the staged models before shipping them
$expected = @{
    'yolox_nano.onnx' = 'c789161ed43c8269fcd4e67c67eeeb4e80c622da2eb296a20bc6007bd18a0b7d'
    'yolox_tiny.onnx' = '427cc366d34e27ff7a03e2899b5e3671425c262ea2291f88bb942bc1cc70b0f7'
}
foreach ($m in $expected.Keys) {
    $h = (Get-FileHash -Algorithm SHA256 "$stage\data\models\$m").Hash.ToLower()
    if ($h -ne $expected[$m]) { throw "staged model $m has the wrong hash" }
}

# ---- ZIP: folder layout that drops straight into C:\ProgramData\obs-studio\plugins
$zipRoot = Join-Path $rel 'zip'
New-Item -ItemType Directory -Force -Path $zipRoot | Out-Null
Copy-Item -Recurse $stage (Join-Path $zipRoot 'vmedia-ai-autoframe')
Copy-Item docs\INSTALLATION.md (Join-Path $zipRoot 'INSTALL.md')
$zip = Join-Path $rel "VMedia-AI-AutoFrame-$version-x64.zip"
Compress-Archive -Path "$zipRoot\*" -DestinationPath $zip -CompressionLevel Optimal
Remove-Item -Recurse -Force $zipRoot

# ---- Installer
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 (ISCC.exe) not found - winget install JRSoftware.InnoSetup' }
& $iscc /Q "/DAppVersion=$version" "/DStageDir=$stage" "/DOutputDir=$rel" installer\installer.iss
if ($LASTEXITCODE) { throw 'Inno Setup failed' }

# ---- Checksums
$sums = Get-ChildItem $rel -File | Where-Object { $_.Extension -in '.exe', '.zip' } | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLower(), $_.Name
}
$sums | Set-Content -Encoding ascii (Join-Path $rel 'SHA256SUMS.txt')
Copy-Item RELEASE_NOTES.md $rel

Write-Host "`nRelease ${version}:" -ForegroundColor Green
Get-ChildItem $rel -File | ForEach-Object { '  {0,-42} {1,8:N1} MB' -f $_.Name, ($_.Length / 1MB) }
Get-Content (Join-Path $rel 'SHA256SUMS.txt')
