cl.exe /W4 /WX /MP /EHsc ^
    cobs.c ^
    tests/test_cobs_decode.cc ^
    tests/test_cobs_decode_inplace.cc ^
    tests/test_cobs_encode_max.cc ^
    tests/test_cobs_encode.cc ^
    tests/test_cobs_encode_inc.cc ^
    tests/test_cobs_encode_inplace.cc ^
    tests/test_paper_figures.cc ^
    tests/test_wikipedia.cc ^
    tests/unittest_main.cc ^
    /link /out:cobs_unittests.exe || exit /b 1
cobs_unittests.exe || exit /b 1
