# nanocobs

`nanocobs` is a C99 implementation of the [Consistent Overhead Byte Stuffing](https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing) ("COBS") algorithm, defined in the [paper](http://www.stuartcheshire.org/papers/COBSforToN.pdf) by Stuart Cheshire and Mary Baker.

Users can encode and decode data in-place or into separate target buffers. The `nanocobs` runtime requires no extra memory overhead. No standard library headers are included, and no standard library functions are called.

## Rationale

Some communication buses (e.g. two-wire UART) are inherently unreliable and have no built-in flow control, integrity guarantees, etc. Multi-hop ecosystem protocols (e.g. Device (BLE) Phone (HTTPS) Server) can also be unreliable, despite being comprised entirely of reliable protocols! Adding integrity checks like CRC can help, but only if the data is [framed](https://en.wikipedia.org/wiki/Frame_(networking)); without framing, the receiver and transmitter are unable to agree exactly what data needs to be retransmitted when loss is detected. Loss does not always have to be due to interference or corruption during transmission, either. Application-level backpressure can exhaust receiver-side storage and result in dropped frames.

Traditional solutions (like [CAN](https://en.wikipedia.org/wiki/CAN_bus)) rely on [bit stuffing](https://en.wikipedia.org/wiki/Bit_stuffing) to define frame boundaries. This works fine, but can be subtle and complex to implement in software without dedicated hardware.

`nanocobs` is not a general-purpose reliability solution, but it can be used as the lowest-level framing algorithm required by a reliable transport.

You probably only need `nanocobs` for things like inter-chip communications protocols on embedded devices. If you already have a reliable transport from somewhere else, you might enjoy using that instead of building your own :)

### Why another COBS?

There are a few out there, but I haven't seen any that optionally encode in-place. This can be handy if you're memory-constrained and would enjoy CPU + RAM optimizations that come from using small frames. Also, the cost of in-place decoding is only as expensive as the number of zeroes in your payload; exploiting that if you're designing your own protocols can make decoding very fast.

Also I didn't see as many unit tests as I'd have liked in the other libraries, especially around invalid payload handling. Framing protocols make for lovely attack surfaces, and malicious COBS frames can easily instruct decoders to jump outside of the frame itself.

## Metrics

It's pretty small.
```
❯ arm-none-eabi-gcc -mthumb -mcpu=cortex-m4 -Os -c cobs.c
❯ arm-none-eabi-nm --print-size --size-sort cobs.o

0000007a 00000018 T cobs_encode_max      (24 bytes)
00000048 00000032 T cobs_decode_inplace  (50 bytes)
00000000 00000048 T cobs_encode_inplace  (72 bytes)
00000092 0000007a T cobs_encode          (122 bytes)
0000010c 0000008c T cobs_decode          (140 bytes)
```

## Usage

Compile `cobs.c` and link it into your app. `#include "path/to/cobs.h"` in your source code. Call functions.

### Encoding With Separate Buffers

Fill a buffer with the data you'd like to encode. Prepare a larger buffer to hold the encoded data. Then, call `cobs_encode` to encode the data into the destination buffer.

```
char decoded[64];
unsigned const len = fill_with_decoded_data(decoded);

char encoded[128];
unsigned encoded_len;
cobs_ret_t const result = cobs_encode(decoded, len, encoded, sizeof(encoded), &encoded_len);

if (result == COBS_RET_SUCCESS) {
  // encoding succeeded, 'encoded' and 'encoded_len' hold details.
} else {
  // encoding failed, look to 'result' for details.
}
```

### Encoding In-Place

The COBS protocol requires an extra byte at the beginning and end of the payload. If encoding and decoding in-place, it becomes your responsibility to reserve these extra bytes. It's easy to mess this up and just put your own data at byte 0, but your data must start at byte 1. For safety and sanity, `cobs_encode_inplace` will error with `COBS_RET_ERR_BAD_PAYLOAD` if the first and last bytes aren't explicitly set to the sentinel value. You have to put them there.

(Note that `64` is an arbitrary size in this example, you can use any size you want up to `COBS_INPLACE_SAFE_BUFFER_SIZE`)

```
char buf[64];
buf[0] = COBS_SENTINEL_VALUE; // You have to do this.
buf[63] = COBS_SENTINEL_VALUE; // You have to do this.

// Now, fill buf[1 .. 63] with whatever data you want.

cobs_ret_t const result = cobs_encode_inplace(buf, 64);

if (result == COBS_RET_SUCCESS) {
  // encoding succeeded, 'buf' now holds the encoded data.
} else {
  // encoding failed, look to 'result' for details.
}
```
### Decoding With Separate Buffers

Decoding works similarly; receive an encoded buffer from somewhere, prepare a buffer to hold the decoded data, and call `cobs_decode`.
```
char encoded[128];
unsigned encoded_len;
get_encoded_data_from_somewhere(encoded, &encoded_len);

char decoded[128];
unsigned decoded_len;
cobs_ret_t const result = cobs_decode(encoded, encoded_len, decoded, sizeof(decoded), &decoded_len);

if (result == COBS_RET_SUCCESS) {
  // decoding succeeded, 'decoded' and 'decoded_len' hold details.
} else {
  // decoding failed, look to 'result' for details.
}
```

### Decoding In-Place

There are two ways to decode COBS-encoded data in-place. One is simply by calling `cobs_decode` and pass your buffer to both the `enc` and `out_dec` parameters. Since COBS decoding never needs to revisit decoded bytes, and decoded payloads are always shorter than encoded payloads, this is guaranteed to work.

`cobs_decode_inplace` is also provided and offers byte-layout-parity to `cobs_encode_inplace`. This lets you, for example, decode a payload, change some bytes, and re-encode it all in the same buffer:

Accumulate data from your source until you encounter a COBS frame delimiter byte of `0x00`. Once you've got that, call `cobs_decode_inplace` on that region of a buffer to do an in-place decoding. The zeroth and zero bytes of your payload will be replaced with the `COBS_SENTINEL_VALUE` bytes that, were you _encoding_ in-place, you would have had to place there anyway.

```
char buf[64];

// You fill 'buf' with an encoded cobs frame (from uart, etc) that ends with 0x00.
unsigned const length = you_fill_buf_with_data(buf);

cobs_ret_t const result = cobs_decode_inplace(buf, length);
if (result == COBS_RET_SUCCESS) {
  // decoding succeeded, 'buf' bytes 0 and length-1 are COBS_SENTINEL_VALUE.
  // your data is in 'buf[1 ... length-2]'
} else {
  // decoding failed, look to 'result' for details.
}
```
## Developing

`nanocobs` uses [catch2](https://github.com/catchorg/Catch2) for unit and functional testing; its unified mega-header is checked in to the `tests` directory. To build and run all tests on macOS or Linux, run `make -j` from a terminal. To build + run all tests on Windows, run the `vsvarsXX.bat` of your choice to set up the VS environment, then run `make-win.bat` (if you want to make that part better, pull requests are very welcome).

Also, it's really amazing how long it takes to compile the Catch2 amalgamated file, thanks C++!

The presubmit workflow compiles `nanocobs` on macOS, Linux (gcc) 32/64, Windows (msvc) 32/64. It also builds weekly against a fresh docker image so I know when newer stricter compilers break it.
