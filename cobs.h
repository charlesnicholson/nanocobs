#pragma once

typedef enum {
  COBS_RET_SUCCESS = 0,
  COBS_RET_ERR_BAD_ARG,
  COBS_RET_ERR_BAD_PAYLOAD,
  COBS_RET_ERR_EXHAUSTED
} cobs_ret_t;

enum {
  // All COBS frames end with this value. If you're scanning a data source
  // for frame delimiters, the presence of this zero byte indicates the
  // completion of a frame.
  COBS_FRAME_DELIMETER = 0x00,

  // In-place encoding mandatory placeholder byte values.
  COBS_INPLACE_SENTINEL_VALUE = 0x5A,

  // In-place encodings that fit in a buffer of this size will always succeed.
  COBS_INPLACE_SAFE_BUFFER_SIZE = 256
};

#ifdef __cplusplus
extern "C" {
#endif


// cobs_encode_inplace
//
// Encode in-place the contents of the provided buffer |buf| of length |len|.
// Returns COBS_RET_SUCCESS on successful encoding.
//
// Because encoding adds leading and trailing bytes, your buffer must reserve
// bytes 0 and len-1 for the encoding. If the first and last bytes of |buf|
// are not set to COBS_INPLACE_SENTINEL_VALUE, the function will fail with
// COBS_RET_ERR_BAD_PAYLOAD.
//
// If a null pointer or invalid length are provided, the function will fail
// with COBS_RET_ERR_BAD_ARG.
//
// If |len| is less than or equal to COBS_INPLACE_SAFE_BUFFER_SIZE, the
// contents of |buf| will never cause encoding to fail. If |len| is larger
// than COBS_INPLACE_SAFE_BUFFER_SIZE, encoding can possibly fail with
// COBS_RET_ERR_BAD_PAYLOAD if there are more than 254 bytes between zeros.
//
// If the function returns COBS_RET_ERR_BAD_PAYLOAD, the contents of |buf| are
// left indeterminate and must not be relied on to be fully encoded or decoded.
cobs_ret_t cobs_encode_inplace(void *buf, unsigned len);


// cobs_decode_inplace
//
// Decode in-place the contents of the provided buffer |buf| of length |len|.
// Returns COBS_RET_SUCCESS on successful decoding.
//
// Because decoding is in-place, the first and last bytes of |buf| will be set
// to the value COBS_INPLACE_SENTINEL_VALUE if decoding succeeds. The decoded
// contents are stored in the inclusive span defined by buf[1] and buf[len-2].
//
// If a null pointer or invalid length are provided, the function will fail
// with COBS_RET_ERR_BAD_ARG.
//
// If the encoded buffer contains any code bytes that exceed |len|, the
// function will fail with COBS_RET_ERR_BAD_PAYLOAD. If the buffer starts
// with a 0 byte, or ends in a nonzero byte, the function will fail with
// COBS_RET_ERR_BAD_PAYLOAD.
//
// If the function returns COBS_RET_ERR_BAD_PAYLOAD, the contents of |buf| are
// left indeterminate and must not be relied on to be fully encoded or decoded.
cobs_ret_t cobs_decode_inplace(void *buf, unsigned len);


// cobs_encode_max
//
// Returns the maximum possible size in bytes of the buffer required to encode
// a buffer of length |dec_len|. Cannot fail.
unsigned cobs_encode_max(unsigned dec_len);


// cobs_encode
//
// Encode |dec_len| decoded bytes from |dec| into |out_enc|, storing the encoded
// length in |out_enc_len|. Returns COBS_RET_SUCCESS on successful encoding.
//
// If any of the input pointers are null, or if any of the lengths are invalid,
// the function will fail with COBS_RET_ERR_BAD_ARG.
//
// If the encoding exceeds |enc_max| bytes, the function will fail with
// COBS_RET_ERR_EXHAUSTED.
cobs_ret_t cobs_encode(void const *dec,
                       unsigned dec_len,
                       void *out_enc,
                       unsigned enc_max,
                       unsigned *out_enc_len);


// cobs_decode
//
// Decode |enc_len| encoded bytes from |enc| into |out_dec|, storing the decoded
// length in |out_dec_len|. Returns COBS_RET_SUCCESS on successful decoding.
//
// If any of the input pointers are null, or if any of the lengths are invalid,
// the function will fail with COBS_RET_ERR_BAD_ARG.
//
// If |enc| starts with a 0 byte, or does not end with a 0 byte, the function
// will fail with COBS_RET_ERR_BAD_PAYLOAD.
//
// If the decoding exceeds |dec_max| bytes, the function will fail with
// COBS_RET_ERR_EXHAUSTED.
cobs_ret_t cobs_decode(void const *enc,
                       unsigned enc_len,
                       void *out_dec,
                       unsigned dec_max,
                       unsigned *out_dec_len);

#ifdef __cplusplus
}
#endif
