@echo off
setlocal
cl /O2 stem.c
if errorlevel 1 exit /b 1
echo Built: stem.exe
