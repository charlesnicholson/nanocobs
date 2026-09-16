-- The Arm cross compiler `make size` uses and the Python the qemu probes are driven
-- by, so neither needs a system-wide install. The cache lives under build/.

-- @envy schema "1"
-- @envy version "0.3.2"
-- @envy sha256sums "0b1191eede2d444386ed4f2008456496f4a23e957b928b3b640a663806b29a64"
-- @envy bin "bin"
-- @envy cache-local "build/envy-cache"
-- @envy deploy "true"
-- @envy root "true"

BUNDLES = {
  ["envy"] = {
    identity = "envy.package-specs@r2",
    source = "https://github.com/envy-package-manager/package-specs.git",
    ref = "4abc43074b424400f7d518ef925f8ab8d4624060",
  },
}

PACKAGES = {
  { spec = "local.armgcc@r0",
    source = "envy/local.armgcc.lua",
    options = { version = "15.2.rel1" } },

  { spec = "envy.python@r1", bundle = "envy",
    options = { version = "3.13.14", release = "20260623",
                provide_python = true, provide_python3 = true } },
}
