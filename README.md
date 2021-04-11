# nanocobs

`nanocobs` is a C99 implementation of a subset of the [Consistent Overhead Byte Stuffing](https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing) ("COBS") algorithm, defined in the [paper](http://www.stuartcheshire.org/papers/COBSforToN.pdf) by Stuart Cheshire and Mary Baker.

User-provided buffers are encoded and decoded in-place, requiring no extra memory overhead. No standard library headers are included, and no standard library functions are called.

## Rationale

Some communication buses (e.g. UART) are inherently unreliable and have no built-in flow control, integrity guarantees, etc. Multi-hop ecosystem protocols (e.g. Device <=BLE=> Phone <=HTTPS=> Server) can also be unreliable, despite being comprised entirely of reliable protocols! Adding integrity checks like CRC can help, but only if the data is [framed](https://en.wikipedia.org/wiki/Frame_(networking)); without framing, the receiver and transmitter are unable to agree exactly what data needs to be retransmitted when loss is detected. Loss does not always have to be due to interference or corruption during transmission, either. Application-level backpressure can exhaust receiver-side storage and result in dropped frames.

Traditional solutions (like [CAN](https://en.wikipedia.org/wiki/CAN_bus)) rely on [bit stuffing](https://en.wikipedia.org/wiki/Bit_stuffing) to define frame boundaries. This works fine, but can be subtle and complex to implement in software without dedicated hardware.

`nanocobs` is not a general-purpose reliability solution, but it can be used as the lowest-level framing algorithm required by a reliable transport.

You probably only need `nanocobs` for things like inter-chip communications protocols on embedded devices. If you already have a reliable transport from somewhere else, you might enjoy using that instead of building your own :)

## Usage

Compile `cobs.c` and link it into your app. `#include "path/to/cobs.h"` in your source code.

### Encoding

Because `nanocobs` operates in-place, and the protocol requires an extra byte at the beginning and end of the payload. It's easy to mess this up and just put your own data at byte 0, but your data must start at byte 1. For safety and sanity, `cobs_encode` will error with `COBS_RET_ERR_BAD_PAYLOAD` if the first and last bytes aren't explicitly set to the sentinel value. You have to put them there.

```
char buf[64];
buf[0] = COBS_SENTINEL_VALUE; // You have to do this.
buf[63] = COBS_SENTINEL_VALUE; // You have to do this.

// Now, fill buf[1 .. 63] with whatever data you want.

cobs_ret_t const result = cobs_encode(buf, 64);

if (result == COBS_RET_SUCCESS) {
  // encoding succeeded
} else {
  // look to result for details (bad args, no sentinels, long non-zero run, etc)
}
```

### Decoding

You are responsible for accumulating data until you encounter a frame delimiter byte of `0x00`. Once you've got that, call `cobs_decode` on that region of a buffer to do an in-place decoding. The zeroth and final bytes of your payload will be replaced with the `COBS_SENTINEL_VALUE` bytes that, were you _encoding_, you would have had to place there anyway.

```
char buf[64];

// You fill 'buf' with an encoded cobs frame (from uart, etc) that ends with 0x00.
unsigned const length = you_fill_buf_with_data(buf);

cobs_ret_t const result = cobs_decode(buf, length);
if (result == COBS_RET_SUCCESS) {
  // decoding succeeded, bytes 0 and length are COBS_SENTINEL_VALUE.
  // your data is in [1 ... length - 2]
} else {
  // look to result for details (bad args, bad payload)
}
```
## Deviations

The COBS paper reserves the literal `0xFF` to indicate "254 data bytes *not* followed by a zero" (Page 4, Table 1). This situation occurs when a run of more than 254 non-zero bytes occur in the buffer, leaving the algorithm no ability to represent "distance to next zero" in a single byte. When this happens, an extra overhead code byte must be _inserted_ into the encoding stream 255 bytes later to indicate the distance to the next zero (or another special `0xFF` literal).

This first implementation of `nanocobs` does not support this feature. Without it, there's a simple guarantee that the only bytes mutated during encoding and decoding are the sentinel bytes and the interior zero bytes. Note that in the current implementation, `nanocobs` does support large payloads as long as they don't have interior non-zero-byte runs greater than 254 bytes in length. If an interior non-zero-byte run longer than 254 bytes is encountered, encoding stops and `COBS_RET_ERR_BAD_PAYLOAD` is returned. The buffer is left in a partially-encoded indeterminate state.

`nanocobs` should support this feature, but it will change the API and introduce more overhead. `cobs_encode` can no longer work in-place, and will need to take an additional larger `output` buffer, and support the new failure mode of "when _inserting_ new overhead `0xFF` code bytes, the `output` buffer was exhausted." Adding this functionality might take the form of a new encode function, or a "strict" option that immediately errors out (like the current behavior) if new overhead bytes need to be inserted. Perhaps something like `cobs_encode` and `cobs_encode_in_place` that share common functionality under the hood.

In the meantime, it means that frames have a maximum size of 256 bytes, and the user-available portion is 254 bytes. That's 122 frames per second over a 250kbps UART link, approximately 8msec per frame.

Finally, Zero Pair Elimination (ZPE) is not implemented. The ZPE variant, described in the paper at Section III Subsection D, further carves up the code-byte space to include a range reserved to describe _pairs_ of zeros at the destination offset.

## Developing

`nanocobs` uses [catch2](https://github.com/catchorg/Catch2) for unit and functional testing; its unified mega-header is checked in to the `tests` directory. To build and run all tests, just run `make` from a terminal after cloning.
