<#
.SYNOPSIS
  Verifies that the libobs ABI the plugin uses is identical between the OBS headers the
  plugin is built against (oldest supported OBS) and a newer OBS.
  Usage: scripts\abi-check.ps1 [-Old third_party\obs-studio-30.0.0] [-New third_party\obs-studio]
#>
param(
    [string]$Old = 'third_party\obs-studio-30.0.0',
    [string]$New = 'third_party\obs-studio'
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$out = Join-Path $Root 'build\abi'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$results = @{}
foreach ($pair in @(@('old', $Old), @('new', $New))) {
    $tag = $pair[0]; $src = Resolve-Path $pair[1]
    $inc = Join-Path $out "inc-$tag"
    if (Test-Path $inc) { Remove-Item -Recurse -Force $inc }
    robocopy (Join-Path $src 'libobs') $inc *.h *.hpp /S /NFL /NDL /NJH /NJS | Out-Null
    foreach ($h in 'pthread.h', 'sched.h', 'semaphore.h') { Copy-Item (Join-Path $src "deps\w32-pthreads\$h") $inc }
    Copy-Item (Join-Path $Root 'third_party\obs-sdk\include\obs\obsconfig.h') $inc
    $exe = Join-Path $out "probe-$tag.exe"
    cmd /c "`"$vcvars`" >nul && cl /nologo /EHsc /I`"$inc`" /Fe`"$exe`" /Fo`"$out\\`" tools\abi-probe.cpp >`"$out\cl-$tag.log`" 2>&1"
    if ($LASTEXITCODE) { Get-Content "$out\cl-$tag.log"; throw "probe failed to compile against $src" }
    $results[$tag] = & $exe
}
$diff = Compare-Object ($results['old'] | Where-Object { $_ -notmatch '^LIBOBS_API' }) ($results['new'] | Where-Object { $_ -notmatch '^LIBOBS_API' })
$results['old'] | Where-Object { $_ -match '^LIBOBS_API' }
$results['new'] | Where-Object { $_ -match '^LIBOBS_API' }
if ($diff) {
    $diff | Format-Table -AutoSize
    throw 'ABI DIFFERENCES FOUND'
}
Write-Host "ABI identical: $(($results['old']).Count - 1) checked values match between $Old and $New"
