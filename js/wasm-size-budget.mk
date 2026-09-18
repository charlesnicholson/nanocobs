# Chrome and Firefox throw on synchronous WebAssembly.Module above 4 KiB on the main
# thread. This is that API limit, so the build fails rather than the browser.
COBS_WASM_SIZE_MAX := 4096
