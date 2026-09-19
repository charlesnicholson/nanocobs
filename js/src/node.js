// SPDX-License-Identifier: Unlicense OR 0BSD
//
// The Node entry point: the native addon when this host has a prebuild, the wasm
// implementation otherwise. Both expose the same API and are held to it by
// js/test/conformance.test.mjs, so which one loaded is a performance question rather
// than a behavioural one.
//
// Only package.json's "node" export condition reaches this file. Browsers, bundlers,
// Deno and Workers resolve straight to ./index.js and never see node-gyp-build, so
// nothing here has to survive being bundled for the web.
//
// `backend` is exported because a silent fallback is the thing that bites: without it
// a deployment can lose the native path -- an unusual libc, a missing prebuild, a
// bundler that did not carry the binaries -- and look exactly like one that still has
// it. Log it at startup.
//
// NANOCOBS_BACKEND=wasm forces the portable path with no code change; =native turns a
// missing prebuild into a startup error instead of a quiet fallback.

import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { dirname } from 'node:path';
import { createNativeCodec } from './native.js';
import * as wasm from './index.js';

export {
  MAX_PAYLOAD_BYTES, CobsError, encodeMax, decodeMax, wasmModule, createCodec,
} from './index.js';

// What createNativeCodec() calls. Checked rather than assumed: a prebuild that loads
// but is missing an entry point would otherwise report backend 'native' and then throw
// on every call, which is a worse failure than falling back.
const REQUIRED = ['encode', 'decode', 'encodeInto', 'decodeInto'];

function loadNative() {
  const require = createRequire(import.meta.url);
  const pkgRoot = dirname(dirname(fileURLToPath(import.meta.url)));
  try {
    // node-gyp-build owns the platform/arch/libc resolution and the prebuilds layout.
    // Using it rather than hand-rolling that is also what lets bundlers trace the
    // .node files: the path it builds is not one static analysis could follow, so
    // tooling recognises the loader instead.
    const addon = require('node-gyp-build')(pkgRoot);
    const missing = REQUIRED.filter((fn) => typeof addon[fn] !== 'function');
    if (missing.length) {
      throw new Error(`prebuild is missing ${missing.join(', ')}`);
    }
    return addon;
  } catch (e) {
    // Shipping no prebuild for this platform is the ordinary case and says nothing.
    // Anything else means a prebuild was found and would not work -- a truncated
    // download, the wrong architecture, a missing system library -- and falling back
    // from that silently hides a real problem.
    const message = String(e?.message ?? e);
    if (!/No native build was found/i.test(message)) {
      console.warn(`nanocobs: native backend unavailable (${message}); using wasm`);
    }
    return null;
  }
}

function pick() {
  const want = process.env.NANOCOBS_BACKEND;
  if (want === 'wasm') return { impl: wasm, backend: 'wasm' };

  const addon = loadNative();
  if (addon) return { impl: createNativeCodec(addon), backend: 'native' };

  if (want === 'native') {
    throw new Error(
      `nanocobs: NANOCOBS_BACKEND=native, but no usable prebuild for ` +
      `${process.platform}-${process.arch}. Unset it to fall back to wasm.`);
  }
  return { impl: wasm, backend: 'wasm' };
}

const chosen = pick();

/** Which implementation loaded: 'native' or 'wasm'. */
export const backend = chosen.backend;

export const encode = chosen.impl.encode;
export const decode = chosen.impl.decode;
export const decodeFirst = chosen.impl.decodeFirst;
export const decodeFrames = chosen.impl.decodeFrames;
export const encodeInto = chosen.impl.encodeInto;
export const decodeInto = chosen.impl.decodeInto;
export const tryEncodeInto = chosen.impl.tryEncodeInto;
export const tryDecodeInto = chosen.impl.tryDecodeInto;
