@echo off
setlocal
gcc -O2 -std=c99 stem.c -o stem.exe
if errorlevel 1 exit /b 1
echo Built: stem.exe
