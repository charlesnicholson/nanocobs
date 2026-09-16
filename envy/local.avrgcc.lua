-- @envy schema "1"
--
-- Microchip's prebuilt AVR toolchain, for the 16-bit-int probe. Linux x86_64 only:
-- Microchip ships no darwin-arm64 or linux-aarch64 build, so on other hosts the
-- avr target is unavailable rather than silently different.

IDENTITY = "local.avrgcc@r0"
EXPORTABLE = false
PLATFORMS = { "linux" }

local sha256_fingerprints = {
  ["3.7.0.1796-x86_64"] = "a541771cfc167e13a3206037625e15e369c562c289504edca9c7b76743974096",
}

OPTIONS = function(opts)
  envy.options({
    version = {
      required = true,
      choices = { "3.7.0.1796" },
      -- The arch is not validated here: OPTIONS runs for every platform a sync
      -- asks about, and only linux x86_64 is published. FETCH is where it lands.
    },
  })
end

FETCH = function(tmp_dir, opts)
  local key = opts.version .. "-" .. envy.ARCH
  if not sha256_fingerprints[key] then
    error("avr8-gnu-toolchain is published for linux x86_64 only, not " .. envy.ARCH)
  end
  return {
    source = "https://ww1.microchip.com/downloads/aemDocuments/documents/DEV/" ..
        "ProductDocuments/SoftwareTools/avr8-gnu-toolchain-" .. opts.version ..
        "-linux.any." .. envy.ARCH .. ".tar.gz",
    sha256 = sha256_fingerprints[key],
  }
end

STAGE = function(fetch_dir, stage_dir, tmp_dir, opts)
  envy.extract_all(fetch_dir, stage_dir, { strip = 1 })
end

PRODUCTS = {
  ["avr-gcc"] = "bin/avr-gcc" .. envy.EXE_EXT,
}
