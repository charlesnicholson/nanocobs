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
    tests\cobs_encode_max_c.c ^
    || exit /b 1

cl.exe /W4 /WX /MP /EHsc /std:c++20 /c ^
    /Fobuild\tests\ ^
    tests\test_cobs_decode.cc ^
    tests\test_cobs_decode_inc.cc ^
    tests\test_cobs_decode_tinyframe.cc ^
    tests\test_cobs_encode.cc ^
    tests\test_cobs_encode_inc.cc ^
    tests\test_cobs_encode_max.cc ^
    tests\test_cobs_encode_tinyframe.cc ^
    tests\test_many_random_payloads.cc ^
    tests\test_paper_figures.cc ^
    tests\test_wikipedia.cc ^
    tests\unittest_main.cc ^
    || exit /b 1

link.exe /nologo /out:build\cobs_unittests.exe ^
    build\cobs.obj ^
    build\cobs_encode_max_c.obj ^
    build\tests\test_cobs_decode.obj ^
    build\tests\test_cobs_decode_inc.obj ^
    build\tests\test_cobs_decode_tinyframe.obj ^
    build\tests\test_cobs_encode.obj ^
    build\tests\test_cobs_encode_inc.obj ^
    build\tests\test_cobs_encode_max.obj ^
    build\tests\test_cobs_encode_tinyframe.obj ^
    build\tests\test_many_random_payloads.obj ^
    build\tests\test_paper_figures.obj ^
    build\tests\test_wikipedia.obj ^
    build\tests\unittest_main.obj ^
    || exit /b 1

build\cobs_unittests.exe || exit /b 1
