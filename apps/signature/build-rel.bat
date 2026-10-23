@echo off
REM Release build helper. Paths are resolved from this script's own location
REM (%~dp0) rather than hardcoded, so the repo can be moved or renamed without
REM the script silently building nothing -- which is exactly what happened when
REM the tree moved from C:\JM VST to C:\Amanorsac Studio\products\sampler.
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set "CM=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
cd /d "%~dp0"
if not exist "build\CMakeCache.txt" (
  echo === configuring ===
  "%CM%" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
)
echo === building ===
if "%1"=="" (
  "%CM%" --build build --config Release
) else (
  "%CM%" --build build --config Release --target %1
)
