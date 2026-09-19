# nanocobs

Consistent Overhead Byte Stuffing. The [nanocobs](https://github.com/charlesnicholson/nanocobs) C implementation, compiled to WebAssembly.

```sh
npm install nanocobs
```

No dependencies, and **no install script** -- installing never runs a compiler and
cannot fail to build. Where the package carries a prebuilt native addon for your
platform it uses that; everywhere else it falls back to WebAssembly, which is ~2 KB
with zero imports, inlined as base64: no file to load, no `fs`, no `fetch`, no bundler
plugin, no async init.

## Framing

```js
import { encode, decode } from 'nanocobs';

const frame = encode(payload);   // Uint8Array, trailing 0x00 delimiter included
const back = decode(frame);      // Uint8Array
```

`decode` is `cobs_decode`: it stops at the first delimiter and ignores anything after
it, so a buffer holding more than one frame yields the first.

## Reading a stream

`decodeFrames` is the shape a socket loop wants. It copies the buffer into wasm memory
once and walks it there, so it stays linear however many frames the buffer holds:

```js
import { decodeFrames } from 'nanocobs';

let carry = new Uint8Array(0);
socket.on('data', (chunk) => {
  const buf = concat(carry, chunk);
  const { frames, consumed } = decodeFrames(buf);
  for (const f of frames) handle(f);
  carry = buf.subarray(consumed);   // an incomplete trailing frame, if there was one
});
```

`consumed` stops short of a partial frame at the end, so the carry is exactly what the
next read has to complete.

`decodeFirst(buf)` is the one-frame form, returning `{ payload, consumed }`. Prefer
`decodeFrames` for a whole buffer: `decodeFirst` copies all of what it is handed, so a
`decodeFirst(buf.subarray(i))` loop costs O(n²) in the frame count.

## Caller-owned buffers

```js
import { encodeInto, decodeInto, encodeMax, decodeMax } from 'nanocobs';

const frame = new Uint8Array(encodeMax(payload.length));
const n = encodeInto(payload, frame);              // bytes written

const out = new Uint8Array(decodeMax(n));
const m = decodeInto(frame.subarray(0, n), out);   // bytes written
```

`tryEncodeInto` and `tryDecodeInto` are the same calls without exceptions: they return
the byte count, or a negative `cobs_ret_t` (`-1` BAD_ARG, `-2` BAD_PAYLOAD, `-3`
EXHAUSTED).

## Errors

Failures throw a `CobsError` carrying the C's own return code, unchanged:

```js
import { CobsError } from 'nanocobs';

try { decode(buf); }
catch (e) {
  if (CobsError.isCobsError(e)) e.code;   // 'BAD_ARG' | 'BAD_PAYLOAD' | 'EXHAUSTED'
}
```

`isCobsError` is a brand check rather than `instanceof`, so it survives duplicate copies
of the package and cross-realm boundaries.

## Isolation

Every entry point above shares one wasm instance. `createCodec()` returns one with its
own linear memory, to reclaim what a large payload pinned or to isolate a worker:

```js
import { createCodec } from 'nanocobs';

const codec = createCodec();
codec.encode(payload);
codec.memoryBytes;    // linear memory held; grows on demand, never shrinks
```

## Backends

Both backends are the same `cobs.c`, and they are held to identical behaviour -- the
same bytes, the same error codes -- by a conformance suite that runs every case through
each. Which one you get is a performance question, not a behavioural one.

| | |
|---|---|
| Node, with a prebuild for your platform | native addon |
| Node, anywhere else | wasm |
| Browsers, bundlers, Deno, Workers | wasm |

Prebuilds ship for `darwin-arm64`, `darwin-x64`, `linux-x64`, `linux-arm64` (glibc and
musl each) and `win32-x64`. Resolution happens when the module is first imported, not
at install time, so `npm ci --ignore-scripts` still gets the native backend.

```js
import { backend } from 'nanocobs';
console.log(backend);   // 'native' | 'wasm'
```

**Log that at startup.** A missing prebuild falls back silently and looks exactly like
the native path, so this is the only thing that tells you which you are running.

`NANOCOBS_BACKEND=wasm` forces the portable path without a code change.
`NANOCOBS_BACKEND=native` turns a missing prebuild into a startup error instead of a
quiet fallback -- useful in an environment where you are relying on native throughput.
`import 'nanocobs/wasm'` always resolves to wasm.

On a platform with no prebuild you can build the addon yourself; the sources ship in
the tarball:

```sh
npm install nanocobs --ignore-scripts
cd node_modules/nanocobs && npx node-gyp rebuild && \
  mkdir -p prebuilds/$(node -p 'process.platform+"-"+process.arch') && \
  cp build/Release/nanocobs.node prebuilds/$(node -p 'process.platform+"-"+process.arch')/
```

## License

Unlicense OR 0BSD
