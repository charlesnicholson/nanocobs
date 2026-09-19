# Only for building the addon by hand from a published tarball:
#
#     npm install nanocobs --ignore-scripts && cd node_modules/nanocobs && npx node-gyp rebuild
#
# `npm install` never runs this. The package ships prebuilds and falls back to wasm, so
# nothing about installing it needs a compiler. In the repo, `make js-addon` builds the
# host's prebuild directly and this file is only copied into the package.
{
  "targets": [
    {
      "target_name": "nanocobs",
      "sources": ["native/cobs_napi.c", "native/cobs.c"],
      "include_dirs": ["native"],
      "defines": ["NDEBUG"],
      "cflags": ["-O3", "-std=c99"],
      "xcode_settings": {"GCC_OPTIMIZATION_LEVEL": "3"},
      "msvs_settings": {"VCCLCompilerTool": {"Optimization": 2}}
    }
  ]
}
