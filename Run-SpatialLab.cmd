@echo off
set "spatialExe=%~dp0app\SpatialLab\bin\Release\net9.0-windows\SpatialLab.exe"
if not exist "%spatialExe%" (
  echo Build first: powershell -File scripts\build.ps1 -Configuration Release -Test
  pause
  exit /b 1
)
start "" "%spatialExe%"
