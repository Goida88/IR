@echo off
setlocal
g++ -O2 -std=c++17 tokenize.cpp -o tokenize.exe
if errorlevel 1 exit /b 1
echo Built: tokenize.exe
