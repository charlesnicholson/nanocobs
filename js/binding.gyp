# Used by prebuildify (which drives node-gyp) to build the addon, and by anyone
# building it by hand from a published tarball:
#
#     npm install nanocobs --ignore-scripts
#     cd node_modules/nanocobs && npx node-gyp rebuild
#
# `npm install` never runs this. The package ships prebuilds and falls back to wasm,
# so nothing about installing it needs a compiler.
{
  "targets": [
    {
      "target_name": "nanocobs",
      "sources": ["native/cobs_napi.c", "native/cobs.c"],
      "include_dirs": ["native"],
      "defines": ["NDEBUG"],
      "cflags": ["-O3", "-std=c99"],
      "conditions": [
        # One binary for both Mac architectures, so prebuilds/darwin-x64+arm64 covers
        # Intel and Apple Silicon from a single runner. prebuildify's --arch only
        # names the output folder; the slices have to be asked for here.
        ["OS=='mac'", {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-arch", "x86_64", "-arch", "arm64"],
            "OTHER_LDFLAGS": ["-arch", "x86_64", "-arch", "arm64"],
            "GCC_OPTIMIZATION_LEVEL": "3"
          }
        }],
        ["OS=='win'", {
          "msvs_settings": {"VCCLCompilerTool": {"Optimization": 2}}
        }]
      ]
    }
  ]
}
