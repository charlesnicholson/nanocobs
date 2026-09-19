// SPDX-License-Identifier: Unlicense OR 0BSD
//
// The Node entry point: the native backend when this host has a prebuild, the wasm one
// otherwise. Both expose the same API and are held to it by
// js/test/conformance.test.mjs, so which one loaded is a performance question rather
// than a behavioural one.
//
// Only package.json's "node" export condition reaches this file. Browsers, bundlers,
// Deno and Workers resolve straight to ./index.js and never see the loader, so nothing
// here has to survive being bundled for the web.
//
// `backend` is exported because a silent fallback is the thing that bites: without it
// a deployment can lose the native path -- an unusual libc, a missing prebuild -- and
// look exactly like one that still has it. Log it at startup.
//
// NANOCOBS_BACKEND=wasm forces the portable path with no code change; =native turns a
// missing prebuild into a startup error instead of a quiet fallback.

import { loadNative } from './loader.js';
import { createNativeCodec } from './native.js';
import * as wasm from './index.js';

export {
  MAX_PAYLOAD_BYTES, CobsError, encodeMax, decodeMax, wasmModule, createCodec,
} from './index.js';

function pick() {
  const want = process.env.NANOCOBS_BACKEND;
  if (want === 'wasm') return { impl: wasm, backend: 'wasm' };

  const addon = loadNative();
  if (addon) return { impl: createNativeCodec(addon), backend: 'native' };

  if (want === 'native') {
    throw new Error(
      `nanocobs: NANOCOBS_BACKEND=native, but no prebuild for ` +
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
