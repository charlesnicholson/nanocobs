-- @envy schema "1"
--
-- wasi-sdk, for the wasm32 build of the npm package. We compile -nostdlib against
-- wasm32-unknown-unknown, so the sysroot is never linked. Over a full LLVM release
-- it buys a tenth the download and a wasm-ld sitting next to its clang.

IDENTITY = "local.wasisdk@r0"
EXPORTABLE = false

-- wasi-sdk publishes every host triple this repo runs on, so nothing to exclude.
local function platform_suffix()
  if envy.PLATFORM == "darwin" then
    return "macos"
  elseif envy.PLATFORM == "linux" then
    return "linux"
  else
    return "windows"
  end
end

local sha256_fingerprints

OPTIONS = function(opts)
  local seen, choices = {}, {}
  for key in pairs(sha256_fingerprints) do
    local v = key:match("^([^-]+)")
    if v and not seen[v] then
      seen[v] = true; choices[#choices + 1] = v
    end
  end
  table.sort(choices)

  envy.options({
    version = {
      required = true,
      choices = choices,
      validate = function(v)
        local key = v .. "-" .. envy.ARCH .. "-" .. platform_suffix()
        if not sha256_fingerprints[key] then
          return "unsupported version/platform: " .. key
        end
      end,
    },
  })
end

-- The tag drops the patch component ("wasi-sdk-34"); the asset keeps it ("34.0").
FETCH = function(tmp_dir, opts)
  local key = opts.version .. "-" .. envy.ARCH .. "-" .. platform_suffix()
  local tag = "wasi-sdk-" .. opts.version:match("^(%d+)")
  local filename = "wasi-sdk-" .. opts.version .. "-" .. envy.ARCH .. "-" ..
      platform_suffix() .. ".tar.gz"

  return {
    source = "https://github.com/WebAssembly/wasi-sdk/releases/download/" ..
        tag .. "/" .. filename,
    sha256 = sha256_fingerprints[key],
  }
end

STAGE = function(fetch_dir, stage_dir, tmp_dir, opts)
  envy.extract_all(fetch_dir, stage_dir, { strip = 1 })
end

-- wasm32-clang, not clang: the wrapper prepends bin/ to PATH, so a bin/clang would
-- shadow the host compiler. clang still finds wasm-ld, since it execs the real path.
PRODUCTS = {
  ["wasm32-clang"] = "bin/clang" .. envy.EXE_EXT,
  ["wasm32-llvm-objdump"] = "bin/llvm-objdump" .. envy.EXE_EXT,
}

sha256_fingerprints = {
  ["34.0-arm64-macos"] =
  "9c59398106b417f8f14913380fdf0097a8cc0ff4af9eb3ce0065a859e88d49e9",
  ["34.0-x86_64-macos"] =
  "87d27fa8adc68dee59bfbf2e22a6d34ef717c34d6bf1d8af2a56fc929d9ce0eb",
  ["34.0-arm64-linux"] =
  "f7e243dff54d60bcc576e94d6166b69f410f2500ae4a9ceef34315be10e77971",
  ["34.0-x86_64-linux"] =
  "b761e3a0721dbae9c09a0059e5fdb2bf917d1b4a8a7b430fb3b5aafb0984b2c4",
  ["34.0-arm64-windows"] =
  "45e1c71f3e965621e7b98ebe1d37b0e4b1f77f3e8072113ffb4534e67b1a4b7c",
  ["34.0-x86_64-windows"] =
  "cccb5c323a9b34f0349a9b09e8804a0a7632c68c3310f4b5f437ed57d7e71d8f",
}
