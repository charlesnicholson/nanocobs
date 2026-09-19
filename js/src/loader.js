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
  // The bare target is the fallback for platforms where libc is not a variable.
  for (const target of [`${base}${libcSuffix()}`, base]) {
    try {
      return require(join(here, '..', 'prebuilds', target, 'nanocobs.node'));
    } catch {
      // Wrong platform, or a prebuild this tarball does not carry. Try the next.
    }
  }
  return null;
}
