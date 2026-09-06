# 네이티브 DLL과 WPF 앱을 빌드하고 -Test 지정 시 기존 검증을 실행한다.
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$spatialRoot = Split-Path -Parent $PSScriptRoot
$spatialCmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $spatialCmake)) {
    $spatialCmake = (Get-Command cmake -ErrorAction Stop).Source
}
$spatialCTest = Join-Path (Split-Path -Parent $spatialCmake) 'ctest.exe'
Push-Location $spatialRoot
try {
    & $spatialCmake --preset windows-x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & $spatialCmake --build --preset $Configuration.ToLowerInvariant()
    if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
    dotnet build app/SpatialLab/SpatialLab.csproj -c $Configuration --ignore-failed-sources -v minimal
    if ($LASTEXITCODE -ne 0) { throw 'WPF build failed.' }
    if ($Test) {
        dotnet build tests/SpatialLab.ViewModelTests/SpatialLab.ViewModelTests.csproj -c $Configuration --ignore-failed-sources -v minimal
        if ($LASTEXITCODE -ne 0) { throw 'ViewModel test build failed.' }
        dotnet "tests/SpatialLab.ViewModelTests/bin/$Configuration/net9.0/SpatialLab.ViewModelTests.dll"
        if ($LASTEXITCODE -ne 0) { throw 'ViewModel tests failed.' }
        & $spatialCTest --test-dir out/build/windows-x64 -C $Configuration --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }
        $spatialExe = Join-Path $spatialRoot "app\SpatialLab\bin\$Configuration\net9.0-windows\SpatialLab.exe"
        $spatialOutput = Join-Path $spatialRoot "out\validation\$($Configuration.ToLowerInvariant())"
        $spatialArguments = @('--smoke', ('"' + $spatialOutput + '"'))
        $spatialProcess = Start-Process -FilePath $spatialExe -ArgumentList $spatialArguments -WindowStyle Hidden -PassThru
        if (-not $spatialProcess.WaitForExit(30000)) {
            $spatialProcess.Kill()
            throw 'WPF smoke test timed out.'
        }
        Get-Content -LiteralPath (Join-Path $spatialOutput 'wpf-smoke.txt')
        if ($spatialProcess.ExitCode -ne 0) { throw 'WPF smoke test failed.' }
    }
    Write-Host "Ready: app\SpatialLab\bin\$Configuration\net9.0-windows\SpatialLab.exe"
} finally { Pop-Location }
