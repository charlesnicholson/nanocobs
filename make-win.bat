@echo off
setlocal

if "%1"=="clean" (
    if exist build rd /s /q build
    exit /b 0
)

if not exist build\tests mkdir build\tests

cl.exe /W4 /WX /MP /std:c11 /c ^
    /Fobuild\ ^
    cobs.c ^
    tests\*.c ^
    || exit /b 1

cl.exe /W4 /WX /MP /EHsc /std:c++20 /c ^
    /Fobuild\tests\ ^
    tests\*.cc ^
    || exit /b 1

link.exe /nologo /out:build\cobs_unittests.exe ^
    build\*.obj ^
    build\tests\*.obj ^
    || exit /b 1

build\cobs_unittests.exe -m -tse=slow || exit /b 1
