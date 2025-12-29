@echo off
setlocal
cl /O2 /std:c++17 tokenize.cpp
if errorlevel 1 exit /b 1
echo Built: tokenize.exe
