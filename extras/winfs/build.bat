@echo off
echo Building...
:: Directory checks
if not exist "bin" mkdir "bin"
if not exist "bin" echo Source code directroy does not exist && exit /b 
echo Directory bin exists
:: File checks 
if not exist "src/sfs2fs.c" echo File sfs2fs.c does not exist && exit /b
echo File sfs2fs.c exists
:: Building
gcc -O2 -municode -o bin/sfs2fs.exe src/sfs2fs.c -I"C:/Program Files (x86)/WinFsp/inc" -L"C:/Program Files (x86)/WinFsp/lib" -lwinfsp-x64

if not exist bin/sfs2fs.exe echo Failed to compile. Please check the log above for more information && exit /b

:: Libraries
echo Copying libraries...
copy "C:\Program Files (x86)\WinFsp\bin\winfsp-x64.dll" bin\
echo Built to sfs2fs.exe