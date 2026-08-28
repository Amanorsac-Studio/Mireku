@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set CM="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
cd /d "C:\JM VST\apps\signature"
if "%1"=="" (
  %CM% --build build --config Release
) else (
  %CM% --build build --config Release --target %1
)
