<#
.SYNOPSIS
  Prepares every third-party dependency needed to build V Media AI AutoFrame.

.DESCRIPTION
  Idempotent. Creates third_party/ with:
    obs-studio-<ObsApiVersion>/  OBS headers the PLUGIN is compiled against. libobs refuses
                                 plugins built with newer headers than the running OBS, so
                                 this is the OLDEST supported OBS (30.0 -> runs on 30..33+).
    obs-studio/                  OBS $ObsVersion headers, used by the integration test host
    obs-sdk/      include/ (plugin API) + include-host/ + lib/ (import libraries generated
                  from obs.dll by symbol name)
    ort-*/        ONNX Runtime (DirectML build: CPU + optional DirectX 12 GPU, no CUDA/RTX)
    directml-*/   DirectML redistributable
    models/       YOLOX-Nano / YOLOX-Tiny ONNX models (Apache-2.0), SHA-256 verified

  The obs.dll used to generate import libraries comes from -ObsBinDir (an installed OBS),
  or, if not given and no local OBS is found, from the official OBS release zip.
  scripts/abi-check.ps1 proves the ABI the plugin uses is identical across both versions.
#>
[CmdletBinding()]
param(
    [string]$ObsApiVersion = '30.0.0',
    [string]$ObsVersion = '33.0.1',
    [string]$ObsBinDir = '',
    [string]$OrtVersion = '1.24.4',
    [string]$DirectMLVersion = '1.15.4'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Root = Split-Path -Parent $PSScriptRoot
$TP = Join-Path $Root 'third_party'
$DL = Join-Path $TP 'dl'
New-Item -ItemType Directory -Force -Path $TP, $DL | Out-Null

$Models = @{
    'yolox_nano.onnx' = @{ Url = 'https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_nano.onnx'; Sha = 'c789161ed43c8269fcd4e67c67eeeb4e80c622da2eb296a20bc6007bd18a0b7d' }
    'yolox_tiny.onnx' = @{ Url = 'https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_tiny.onnx'; Sha = '427cc366d34e27ff7a03e2899b5e3671425c262ea2291f88bb942bc1cc70b0f7' }
}

function Get-File([string]$Url, [string]$Dest) {
    if (Test-Path $Dest) { return }
    Write-Host "Downloading $Url"
    $tmp = "$Dest.part"
    Invoke-WebRequest -Uri $Url -OutFile $tmp -UseBasicParsing
    Move-Item -Force $tmp $Dest
}

function Expand-Zip([string]$Zip, [string]$Dest) {
    if (Test-Path $Dest) { return }
    Write-Host "Extracting $(Split-Path -Leaf $Zip)"
    $tmp = "$Dest.tmp"
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    # .nupkg files are zips; Expand-Archive insists on a .zip extension
    $z = $Zip
    if ($Zip -notlike '*.zip') { $z = "$Zip.zip"; Copy-Item -Force $Zip $z }
    Expand-Archive -Path $z -DestinationPath $tmp -Force
    if ($z -ne $Zip) { Remove-Item -Force $z }
    Move-Item $tmp $Dest
}

function Find-MsvcTool([string]$Name) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found - install Visual Studio 2022 (C++ workload) or Build Tools.' }
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'No Visual Studio installation with the MSVC x64 toolset was found.' }
    $tool = Get-ChildItem -Path (Join-Path $vs 'VC\Tools\MSVC') -Recurse -Filter $Name |
        Where-Object { $_.FullName -match 'Hostx64\\x64' } | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $tool) { throw "$Name not found in $vs" }
    return $tool.FullName
}

# ---------------------------------------------------------------- OBS sources (headers)
function Get-ObsSource([string]$Version, [string]$Dest) {
    if (Test-Path (Join-Path $Dest 'libobs\obs.h')) { return }
    Write-Host "Fetching OBS Studio $Version headers (sparse)"
    git clone --depth 1 --branch $Version --filter=blob:none --sparse https://github.com/obsproject/obs-studio.git $Dest
    if ($LASTEXITCODE) { throw 'git clone failed' }
    git -C $Dest sparse-checkout set libobs deps/w32-pthreads
    if ($LASTEXITCODE) { throw 'git sparse-checkout failed' }
}
$ObsSrc = Join-Path $TP 'obs-studio'                       # host / test headers
$ObsApiSrc = Join-Path $TP "obs-studio-$ObsApiVersion"     # plugin API headers
Get-ObsSource $ObsVersion $ObsSrc
Get-ObsSource $ObsApiVersion $ObsApiSrc

# ---------------------------------------------------------------- OBS binaries (for import libs)
if (-not $ObsBinDir) {
    $candidate = Join-Path $env:ProgramFiles 'obs-studio\bin\64bit'
    if (Test-Path (Join-Path $candidate 'obs.dll')) {
        $ver = (Get-Item (Join-Path $candidate 'obs64.exe')).VersionInfo.ProductVersion
        if ($ver -and $ver.StartsWith($ObsVersion)) { $ObsBinDir = $candidate }
        else { Write-Host "Installed OBS is $ver, not $ObsVersion - using the official release zip instead." }
    }
}
if (-not $ObsBinDir) {
    $zip = Join-Path $DL "OBS-Studio-$ObsVersion-Windows-x64.zip"
    Get-File "https://github.com/obsproject/obs-studio/releases/download/$ObsVersion/OBS-Studio-$ObsVersion-Windows-x64.zip" $zip
    $obsBin = Join-Path $TP "obs-bin-$ObsVersion"
    Expand-Zip $zip $obsBin
    $ObsBinDir = (Get-ChildItem -Path $obsBin -Recurse -Filter obs.dll | Select-Object -First 1).DirectoryName
}
Write-Host "Using OBS binaries from $ObsBinDir"

# ---------------------------------------------------------------- OBS SDK (headers + import libs)
$Sdk = Join-Path $TP 'obs-sdk'
$SdkLib = Join-Path $Sdk 'lib'
$stamp = Join-Path $Sdk "sdk-$ObsApiVersion-$ObsVersion.stamp"
if (-not (Test-Path $stamp)) {
    if (Test-Path $Sdk) { Remove-Item -Recurse -Force $Sdk }
    New-Item -ItemType Directory -Force -Path $SdkLib | Out-Null

    # include/      = plugin API (oldest supported OBS)
    # include-host/ = the OBS version the integration test drives
    foreach ($pair in @(@('include', $ObsApiSrc), @('include-host', $ObsSrc))) {
        $dst = Join-Path $Sdk "$($pair[0])\obs"
        New-Item -ItemType Directory -Force -Path $dst | Out-Null
        # libobs headers keep their directory structure (obs.h, util/, graphics/, media-io/, callback/)
        robocopy (Join-Path $pair[1] 'libobs') $dst *.h *.hpp /S /NFL /NDL /NJH /NJS | Out-Null
        foreach ($h in 'pthread.h', 'sched.h', 'semaphore.h') {
            Copy-Item (Join-Path $pair[1] "deps\w32-pthreads\$h") $dst -Force
        }
        # obsconfig.h is generated by OBS's CMake; these are the Windows values for OBS 30+
        @'
#pragma once
#define OBS_DATA_PATH "../../data"
#define OBS_PLUGIN_PATH "../../obs-plugins/64bit"
#define OBS_PLUGIN_DESTINATION "obs-plugins/64bit"
#define OBS_INSTALL_PREFIX ""
#define OBS_RELEASE_CANDIDATE 0
#define OBS_BETA 0
'@ | Set-Content -Encoding ascii (Join-Path $dst 'obsconfig.h')
    }

    $dumpbin = Find-MsvcTool 'dumpbin.exe'
    $libexe = Find-MsvcTool 'lib.exe'
    foreach ($name in 'obs', 'w32-pthreads') {
        $dll = Join-Path $ObsBinDir "$name.dll"
        if (-not (Test-Path $dll)) { throw "$dll not found" }
        $def = Join-Path $SdkLib "$name.def"
        $exports = & $dumpbin /NOLOGO /EXPORTS $dll
        $lines = @("LIBRARY $name", 'EXPORTS')
        $inTable = $false
        foreach ($l in $exports) {
            if ($l -match '^\s+ordinal\s+hint\s+RVA\s+name') { $inTable = $true; continue }
            if ($inTable) {
                if ($l -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)') { $lines += "    $($Matches[1])" }
                elseif ($l -match '^\s+\d+\s+[0-9A-Fa-f]+\s+(\S+)\s+\(forwarded') { $lines += "    $($Matches[1])" }
                elseif ($l -match '^\s*Summary') { break }
            }
        }
        if ($lines.Count -lt 10) { throw "Could not read exports of $dll" }
        $lines | Set-Content -Encoding ascii $def
        & $libexe /NOLOGO /MACHINE:X64 "/DEF:$def" "/OUT:$(Join-Path $SdkLib "$name.lib")" | Out-Null
        if ($LASTEXITCODE) { throw "lib.exe failed for $name" }
        Write-Host ("  {0}.lib  ({1} exports)" -f $name, ($lines.Count - 2))
    }
    Set-Content -Path $stamp -Value "Plugin API: OBS $ObsApiVersion headers. Host: OBS $ObsVersion. Import libs from $ObsBinDir"
}

# ---------------------------------------------------------------- ONNX Runtime + DirectML
$ortPkg = Join-Path $DL "ort-dml-$OrtVersion.nupkg"
Get-File "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/$OrtVersion/microsoft.ml.onnxruntime.directml.$OrtVersion.nupkg" $ortPkg
Expand-Zip $ortPkg (Join-Path $TP "ort-$OrtVersion")
$dmlPkg = Join-Path $DL "directml-$DirectMLVersion.nupkg"
Get-File "https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/$DirectMLVersion/microsoft.ai.directml.$DirectMLVersion.nupkg" $dmlPkg
Expand-Zip $dmlPkg (Join-Path $TP "directml-$DirectMLVersion")

# ---------------------------------------------------------------- Models
$modelDir = Join-Path $TP 'models'
New-Item -ItemType Directory -Force -Path $modelDir | Out-Null
foreach ($m in $Models.Keys) {
    $dest = Join-Path $modelDir $m
    Get-File $Models[$m].Url $dest
    $sha = (Get-FileHash -Algorithm SHA256 $dest).Hash.ToLower()
    if ($sha -ne $Models[$m].Sha) { Remove-Item $dest; throw "SHA-256 mismatch for $m ($sha)" }
}
# Apache-2.0 requires the license text to travel with the models
Get-File 'https://raw.githubusercontent.com/Megvii-BaseDetection/YOLOX/0.1.1rc0/LICENSE' (Join-Path $modelDir 'YOLOX-LICENSE.txt')

Write-Host ''
Write-Host 'Dependencies ready:'
Write-Host "  OBS SDK      $Sdk"
Write-Host "  ONNX Runtime $(Join-Path $TP "ort-$OrtVersion")"
Write-Host "  DirectML     $(Join-Path $TP "directml-$DirectMLVersion")"
Write-Host "  Models       $modelDir"
