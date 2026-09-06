# 선택한 구성의 SpatialLab 실행 파일을 시작한다.
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$spatialRoot = Split-Path -Parent $PSScriptRoot
$spatialExe = Join-Path $spatialRoot "app\SpatialLab\bin\$Configuration\net9.0-windows\SpatialLab.exe"
if (-not (Test-Path -LiteralPath $spatialExe)) {
    throw 'Build first: .\scripts\build.ps1 -Configuration Release -Test'
}
& $spatialExe
