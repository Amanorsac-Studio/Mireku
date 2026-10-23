@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "C:\JM VST\packages\aofas-format\cpp"
cl /std:c++17 /EHsc /nologo /Fe:aofas_test.exe test\aofas_format_test.cpp
