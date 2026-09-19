// SPDX-License-Identifier: Unlicense OR 0BSD
//
// Finds the prebuilt addon for this host, or reports that there isn't one.
//
// Deliberately not node-gyp-build: the package has no dependencies, and without a
// compile-on-install tier there is nothing left for that to do beyond the twenty lines
// below. Resolution happens at require time, not install time, which is why
// `npm ci --ignore-scripts` still gets the native backend.

import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

// glibcVersionRuntime is absent on musl, which is how detect-libc tells them apart.
// Alpine is a normal place to run a Node service, so a glibc-only build would fall
// back silently there.
function libcSuffix() {
  if (process.platform !== 'linux') return '';
  try {
    const header = process.report?.getReport?.()?.header;
    return header?.glibcVersionRuntime ? '-glibc' : '-musl';
  } catch {
    return '-glibc';
  }
}

/** The addon's exports, or null if this host has no prebuild. Never throws. */
export function loadNative() {
  const here = dirname(fileURLToPath(import.meta.url));
  const require = createRequire(import.meta.url);
  const base = `${process.platform}-${process.arch}`;
  // The bare target is the fallback for platforms where libc is not a variable, where
  // it collapses onto the first candidate -- deduped so a broken prebuild is not
  // reported twice.
  for (const target of [...new Set([`${base}${libcSuffix()}`, base])]) {
    const path = join(here, '..', 'prebuilds', target, 'nanocobs.node');
    try {
      return require(path);
    } catch (e) {
      // Not carrying a prebuild for this platform is the ordinary case and says
      // nothing. Carrying one that will not load is a broken install -- a truncated
      // download, the wrong architecture, a missing system library -- and falling
      // back from that silently hides a real problem, so name it once and carry on.
      if (e?.code !== 'MODULE_NOT_FOUND') {
        console.warn(`nanocobs: ${path} exists but failed to load ` +
                     `(${e?.message ?? e}); falling back to wasm`);
      }
    }
  }
  return null;
}
