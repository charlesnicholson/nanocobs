# nanocobs

`nanocobs` is a C99 implementation of the [Consistent Overhead Byte Stuffing](https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing) ("COBS") algorithm, defined in the [paper](http://www.stuartcheshire.org/papers/COBSforToN.pdf) by Stuart Cheshire and Mary Baker.

Users can encode and decode data in-place or into separate target buffers. Encoding and decoding can both be incremental, streaming data through small caller-provided buffers. The `nanocobs` runtime requires no extra memory overhead. No standard library headers are included, and no standard library functions are called.

## Rationale

Some communication buses (e.g. two-wire UART) are inherently unreliable and have no built-in flow control, integrity guarantees, etc. Multi-hop ecosystem protocols (e.g. Device (BLE) Phone (HTTPS) Server) can also be unreliable, despite being comprised entirely of reliable protocols! Adding integrity checks like CRC can help, but only if the data is [framed](https://en.wikipedia.org/wiki/Frame_(networking)); without framing, the receiver and transmitter are unable to agree exactly what data needs to be retransmitted when loss is detected. Loss does not always have to be due to interference or corruption during transmission, either. Application-level backpressure can exhaust receiver-side storage and result in dropped frames.

Traditional solutions (like [CAN](https://en.wikipedia.org/wiki/CAN_bus)) rely on [bit stuffing](https://en.wikipedia.org/wiki/Bit_stuffing) to define frame boundaries. This works fine, but can be subtle and complex to implement in software without dedicated hardware.

`nanocobs` is not a general-purpose reliability solution, but it can be used as the lowest-level framing algorithm required by a reliable transport.

You probably only need `nanocobs` for things like inter-chip communications protocols on embedded devices. If you already have a reliable transport from somewhere else, you might enjoy using that instead of building your own :)

### Why another COBS?

There are a few out there, but I haven't seen any that optionally encode in-place. This can be handy if you're memory-constrained and would enjoy CPU + RAM optimizations that come from using small frames. In-place decoding walks the chain of code bytes and only has to write where your payload had zeroes, so keeping zeroes sparse when you design your own protocols makes decoding very fast.

None of the other COBS implementations I saw supported incremental encoding and decoding. It's often the case in communication stacks that a layer above the link provides a tightly-sized payload buffer, and the link has to encode both a header _and_ this payload into a single frame. That requires an extra buffer for assembling which then immediately gets encoded into yet another buffer. With incremental encoding, data can be streamed through small buffers without ever needing to allocate the full encoded frame.

Finally, I didn't see as many unit tests as I'd have liked in the other libraries, especially around invalid payload handling. Framing protocols make for lovely attack surfaces, and malicious COBS frames can easily instruct decoders to jump outside of the frame itself.

## Speed

Encoding and decoding process a machine word at a time wherever a COBS block
holds a long run of bytes, of either all-zero or all-nonzero; anything shorter
falls back to a byte loop.

Measured on an Apple M3 Pro at `-O2` with 64 KiB payloads, the one-shot APIs run
6x-14x faster than the byte loop on payloads with runs comfortably longer than a
word (sparse zeroes, ASCII text, long zero fills), tapering to roughly break-even
and as low as 0.7x when zeroes and data alternate every few bytes. The
incremental APIs win everywhere: ~1.9x encoding and ~2.7x decoding at a 16-byte
chunk size, rising to ~5.5x / ~7.6x at 256 bytes. `cobs_encode_tinyframe` and
`cobs_decode_tinyframe` run 1.4x-5x on realistic frames.

Run `make bench` to reproduce these on your own machine. It builds both
implementations into one binary and times them A/B interleaved, so the comparison
is immune to thermal drift.

### Turning it off

`COBS_SWAR_WORD_BITS` is a compile-time flag on `cobs.c`:

```
cc -DCOBS_SWAR_WORD_BITS=8 -c cobs.c     # byte loop only
```

The default is the width of `uintptr_t`, floored at 32 bits, and it is disabled
automatically on any target without single-instruction unaligned access
(Cortex-M0/M0+/M23 and anything narrower than 32 bits, which is every 8- and
16-bit part). On those targets `cobs.o` is what it always was; there is nothing
to opt out of. `-DCOBS_SWAR_WORD_BITS=16` opts a 16-bit machine in, if you
measure a win there.

Encoding and decoding are byte-for-byte identical in every configuration. The
test suite proves it: it links a second copy of `cobs.c` built with
`-DCOBS_SWAR_WORD_BITS=8` and diffs the two implementations over an exhaustive
corpus, every source and destination alignment, and every chunk boundary.

## Metrics

It's pretty small, and you probably need either `cobs_[en|de]code_tinyframe` _or_ `cobs_[en|de]code[_inc*]`, but not all of them.

```
❯ make size

> ./bin/arm-none-eabi-gcc -mthumb -mcpu=cortex-m4 -Os -std=c99 -Wall -Wextra -Werror -Wconversion -c cobs.c
> ./bin/arm-none-eabi-nm --print-size --size-sort cobs.o

00000400 0000000e T cobs_decode_inc_begin  (14 bytes)
00000212 0000001c T cobs_encode_inc_begin  (28 bytes)
0000055c 00000044 T cobs_decode            (68 bytes)
00000000 00000056 t flush_block            (86 bytes)
000000c6 0000006e T cobs_decode_tinyframe  (110 bytes)
00000056 00000070 T cobs_encode_tinyframe  (112 bytes)
00000342 000000be T cobs_encode_inc_end    (190 bytes)
00000134 000000de T cobs_encode            (222 bytes)
0000022e 00000114 T cobs_encode_inc        (276 bytes)
0000040e 0000014e T cobs_decode_inc        (334 bytes)
Total 5a0 (1440 bytes)

byte loop only (-DCOBS_SWAR_WORD_BITS=8): 986 bytes
```

The word-at-a-time fast lanes cost 454 bytes of flash on Cortex-M4. On Cortex-M0
and anything narrower they cost nothing: the object is bit-identical to the byte
loop. `make size-check` asserts a budget with headroom, and `make size-nolibc` fails the build if
`cobs.o` ever acquires an undefined symbol, which is what keeps the
no-standard-library promise above honest rather than aspirational.

The cross compiler is pinned in `envy.lua` and fetched on demand by the `bin/`
wrappers, so the numbers above are reproducible without installing anything.
`make` and `make bench` never touch it. Set `ARM_CC` and `ARM_NM` to use your own.

## Usage

Compile `cobs.c` and link it into your app. `#include "path/to/cobs.h"` in your source code. Call functions.

### Encoding

Fill a buffer with the data you'd like to encode. Prepare a larger buffer to hold the encoded data. Then, call `cobs_encode` to encode the data into the destination buffer.

```c
unsigned char decoded[64];
unsigned const len = fill_with_decoded_data(decoded);

unsigned char encoded[128];
unsigned encoded_len;
cobs_ret_t const result = cobs_encode(decoded, len, encoded, sizeof(encoded), &encoded_len);

if (result == COBS_RET_SUCCESS) {
  // encoding succeeded, 'encoded' and 'encoded_len' hold details.
} else {
  // encoding failed, look to 'result' for details.
}
```

### Decoding

Decoding works similarly; receive an encoded buffer from somewhere, prepare a buffer to hold the decoded data, and call `cobs_decode`.

```c
unsigned char encoded[128];
unsigned encoded_len;
get_encoded_data_from_somewhere(encoded, &encoded_len);

unsigned char decoded[128];
unsigned decoded_len;
cobs_ret_t const result = cobs_decode(encoded, encoded_len, decoded, sizeof(decoded), &decoded_len);

if (result == COBS_RET_SUCCESS) {
  // decoding succeeded, 'decoded' and 'decoded_len' hold details.
} else {
  // decoding failed, look to 'result' for details.
}
```

### Incremental Encoding

The incremental encoding API lets you stream COBS-encoded data through small buffers. Each call to `cobs_encode_inc` takes per-call source and destination buffers, reporting how many bytes were consumed and written. A 255-byte work buffer (provided by the caller) holds the current in-progress block internally.

This is ideal for memory-constrained embedded systems that can't allocate `COBS_ENCODE_MAX(n)` bytes up front.

```c
unsigned char work_buf[255];       // user-provided, must remain valid until end()
cobs_enc_ctx_t ctx;
cobs_ret_t r = cobs_encode_inc_begin(&ctx, work_buf, sizeof(work_buf));

unsigned char header[8];
unsigned header_len = get_header(header);

unsigned char out[64];             // small output buffer, reused each call
size_t src_consumed, dst_written;

cobs_encode_inc_args_t args;
args.dec_src = header;
args.enc_dst = out;
args.dec_src_max = header_len;
args.enc_dst_max = sizeof(out);
r = cobs_encode_inc(&ctx, &args, &src_consumed, &dst_written);
// send out[0..dst_written) downstream

unsigned char const *payload;
unsigned payload_len = get_payload(&payload);

// Feed payload in chunks; small output buffers are fine
size_t pos = 0;
while (pos < payload_len) {
  args.dec_src = payload + pos;
  args.enc_dst = out;
  args.dec_src_max = payload_len - pos;
  args.enc_dst_max = sizeof(out);
  r = cobs_encode_inc(&ctx, &args, &src_consumed, &dst_written);
  // send out[0..dst_written) downstream
  pos += src_consumed;
}

// Flush the final block + delimiter (may need multiple calls with tiny buffers)
bool finished = false;
while (!finished) {
  r = cobs_encode_inc_end(&ctx, out, sizeof(out), &dst_written, &finished);
  // send out[0..dst_written) downstream
}
```

If your output buffer is large enough (e.g. `COBS_ENCODE_MAX(n)` bytes), each call consumes all source bytes and `end()` finishes in one call. With smaller output buffers, the encoder pauses mid-flush and resumes on the next call.

### Incremental Decoding

The incremental decoding API mirrors the encoding API. Each call to `cobs_decode_inc` takes per-call source and destination buffers, reporting how many encoded bytes were consumed, how many decoded bytes were written, and whether the frame delimiter has been reached.

```c
cobs_decode_inc_ctx_t ctx;
cobs_ret_t r = cobs_decode_inc_begin(&ctx);

unsigned char enc[64];             // small input buffer, filled from uart/etc
unsigned char dec[64];             // small output buffer
size_t src_consumed, dst_written;
bool complete = false;

while (!complete) {
  unsigned enc_len = read_encoded_chunk(enc, sizeof(enc));

  cobs_decode_inc_args_t args;
  args.enc_src = enc;
  args.dec_dst = dec;
  args.enc_src_max = enc_len;
  args.dec_dst_max = sizeof(dec);
  r = cobs_decode_inc(&ctx, &args, &src_consumed, &dst_written, &complete);
  // process dec[0..dst_written) upstream
}
```

### Tinyframe Encoding

If you can guarantee that your payloads are shorter than 254 bytes, you can use the tinyframe API to encode and decode in-place in a single buffer. The COBS protocol requires an extra byte at the beginning and end of the payload. If encoding and decoding in-place, it becomes your responsibility to reserve these extra bytes. It's easy to mess this up and just put your own data at byte 0, but your data must start at byte 1. For safety and sanity, `cobs_encode_tinyframe` will error with `COBS_RET_ERR_BAD_PAYLOAD` if the first and last bytes aren't explicitly set to the sentinel value. You have to put them there.

(Note that `64` is an arbitrary size in this example, you can use any size you want up to `COBS_TINYFRAME_SAFE_BUFFER_SIZE`)

```c
unsigned char buf[64];
buf[0] = COBS_TINYFRAME_SENTINEL_VALUE; // You have to do this.
buf[63] = COBS_TINYFRAME_SENTINEL_VALUE; // You have to do this.

// Now, fill in buf[1] up to and including buf[62] (so 64 - 2 = 62 bytes of payload data)

cobs_ret_t const result = cobs_encode_tinyframe(buf, 64);

if (result == COBS_RET_SUCCESS) {
  // encoding succeeded, 'buf' now holds the encoded data.
} else {
  // encoding failed, look to 'result' for details.
}
```

### Tinyframe Decoding

`cobs_decode_tinyframe` offers byte-layout-parity with `cobs_encode_tinyframe`. This lets you decode a payload, change some bytes, and re-encode it all in the same buffer.

Accumulate data from your source until you encounter a COBS frame delimiter byte of `0x00`. Once you've got that, call `cobs_decode_tinyframe` on that region of a buffer to do an in-place decoding. The zeroth and final bytes of your payload will be replaced with the `COBS_TINYFRAME_SENTINEL_VALUE` bytes that, were you _encoding_ in-place, you would have had to place there anyway.

```c
unsigned char buf[64];

// You fill 'buf' with an encoded cobs frame (from uart, etc) that ends with 0x00.
unsigned const length = you_fill_buf_with_data(buf);

cobs_ret_t const result = cobs_decode_tinyframe(buf, length);
if (result == COBS_RET_SUCCESS) {
  // decoding succeeded, 'buf' bytes 0 and length-1 are COBS_TINYFRAME_SENTINEL_VALUE.
  // your data is in 'buf[1 ... length-2]'
} else {
  // decoding failed, look to 'result' for details.
}
```

## Developing

`nanocobs` uses [doctest](https://github.com/onqtam/doctest) for unit and functional testing; its unified mega-header is checked in to the `tests` directory. To build and run all tests on macOS or Linux, run `make -j` from a terminal. `make test-all` adds an exhaustive tier that is too slow for the normal edit loop, `make bench` builds and runs the throughput benchmark, and `make size` reports Cortex-M4 code size. To build + run all tests on Windows, run the `vsvarsXX.bat` of your choice to set up the VS environment, then run `make-win.bat` (if you want to make that part better, pull requests are very welcome).

The presubmit workflow compiles `nanocobs` on macOS, Linux (gcc) 32/64, Windows (msvc) 32/64. It also builds weekly against a fresh docker image so I know when newer stricter compilers break it.
