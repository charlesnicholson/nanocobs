-- The cross compilers the size target and the qemu probes need, plus the Python that
-- drives them, so none of it needs a system-wide install.

-- @envy schema "1"
-- @envy version "0.4.13"
-- @envy sha256sums "adfce8b9ef78e4fe5d59728f4175fadc4b20820fc4517bed030b34f394ed1bf3"
-- @envy bin "bin"
-- @envy cache-local "build/envy-cache"
-- @envy deploy "true"
-- @envy root "true"

BUNDLES = {
  ["envy"] = {
    identity = "envy.package-specs@r11",
    source = "https://github.com/envy-package-manager/package-specs.git",
    ref = "25274d663b916b8cb71771b1edbdfb30fd598420",
  },
}

PACKAGES = {
  { spec = "local.armgcc@r0",
    source = "envy/local.armgcc.lua",
    options = { version = "15.2.rel1" } },

  { spec = "envy.python@r5", bundle = "envy",
    options = { version = "3.15.2", release = "20261026",
                provide_python = true, provide_python3 = true } },

  -- wasm32 clang for the npm package's wasm build. `make` and `make bench` never
  -- touch it; only the js-* targets do.
  { spec = "local.wasisdk@r0",
    source = "envy/local.wasisdk.lua",
    options = { version = "34.0" } },

  -- Microchip publishes avr8-gnu-toolchain for linux x86_64 only, so the 16-bit
  -- probe is a linux-only target rather than a differently-built one elsewhere.
  { spec = "local.avrgcc@r0",
    source = "envy/local.avrgcc.lua",
    platforms = { "linux" },
    options = { version = "3.7.0.1796" } },
}

