<#
.SYNOPSIS
  Downloads the test footage into tests/media (not committed: Mixkit's free license allows
  use in projects but not redistribution of the clips themselves).

  23117  presenter walking at a whiteboard, audience in the foreground   (preacher case)
  23521  presenter, a second person walks in                              (subject lock)
  36706  four-piece band                                                  (worship team / group)
  24068  people on stage, large audience heads in the foreground          (stage-area case)
  + 23117_1080p.mp4 (upscaled copy, for the resolution-change test; needs ffmpeg)
#>
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$dir = Join-Path $PSScriptRoot 'media'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$ua = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0 Safari/537.36'
foreach ($id in 23117, 23521, 36706, 24068) {
    $dest = Join-Path $dir "$id.mp4"
    if (Test-Path $dest) { continue }
    foreach ($q in 720, 1080) {
        try {
            Invoke-WebRequest -UserAgent $ua -Uri "https://assets.mixkit.co/videos/$id/$id-$q.mp4" -OutFile $dest -UseBasicParsing
            Write-Host "downloaded $id ($q p)"
            break
        } catch { if (Test-Path $dest) { Remove-Item $dest } }
    }
}
$hd = Join-Path $dir '23117_1080p.mp4'
if (-not (Test-Path $hd) -and (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    ffmpeg -v error -y -i (Join-Path $dir '23117.mp4') -vf scale=1920:1080 -c:v libx264 -preset veryfast -crf 20 -an $hd
}
$still = Join-Path $dir 'still_23117.png'
if (-not (Test-Path $still) -and (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    ffmpeg -v error -y -ss 2 -i (Join-Path $dir '23117.mp4') -frames:v 1 $still
}
Get-ChildItem (Join-Path $dir '*') -Include *.mp4, *.png | ForEach-Object { '{0,-20} {1,8:N1} MB' -f $_.Name, ($_.Length / 1MB) }
