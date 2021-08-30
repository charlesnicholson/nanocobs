#include "cobs.h"

#define COBS_ISV COBS_INPLACE_SENTINEL_VALUE
typedef unsigned char cobs_byte_t;

cobs_ret_t cobs_encode_inplace(void *buf, unsigned len) {
  if (!buf || (len < 2)) { return COBS_RET_ERR_BAD_ARG; }

  cobs_byte_t *const src = (cobs_byte_t *)buf;
  if ((src[0] != COBS_ISV) || (src[len - 1] != COBS_ISV)) {
    return COBS_RET_ERR_BAD_PAYLOAD;
  }

  unsigned patch = 0, cur = 1;
  while (cur < len - 1) {
    if (src[cur] == COBS_FRAME_DELIMETER) {
      unsigned const ofs = cur - patch;
      if (ofs > 255) { return COBS_RET_ERR_BAD_PAYLOAD; }
      src[patch] = (cobs_byte_t)ofs;
      patch = cur;
    }
    ++cur;
  }
  unsigned const ofs = cur - patch;
  if (ofs > 255) { return COBS_RET_ERR_BAD_PAYLOAD; }
  src[patch] = (cobs_byte_t)ofs;
  src[cur] = 0;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_decode_inplace(void *buf, unsigned const len) {
  if (!buf || (len < 2)) { return COBS_RET_ERR_BAD_ARG; }

  cobs_byte_t *const src = (cobs_byte_t *)buf;
  unsigned ofs, cur = 0;
  while ((ofs = src[cur]) != COBS_FRAME_DELIMETER) {
    src[cur] = 0;
    cur += ofs;
    if (cur > len) { return COBS_RET_ERR_BAD_PAYLOAD; }
  }

  if (cur != len - 1) { return COBS_RET_ERR_BAD_PAYLOAD; }
  src[0] = COBS_ISV;
  src[len - 1] = COBS_ISV;
  return COBS_RET_SUCCESS;
}


cobs_ret_t cobs_encode(void const *dec,
                       unsigned dec_len,
                       void *out_enc,
                       unsigned enc_max,
                       unsigned *out_enc_len) {
  if (!dec || !out_enc || !out_enc_len) { return COBS_RET_ERR_BAD_ARG; }
  if ((enc_max < 2) || (enc_max < dec_len)) { return COBS_RET_ERR_BAD_ARG; }

  cobs_byte_t const *const src = (cobs_byte_t const *)dec;
  cobs_byte_t *const dst = (cobs_byte_t *)out_enc;

  unsigned src_idx = 0, dst_code_idx = 0, dst_idx = 1;
  cobs_byte_t code = 1;

  while (dec_len--) {
    cobs_byte_t const byte = src[src_idx];
    if (byte) {
      dst[dst_idx] = byte;
      if (++dst_idx >= enc_max) { return COBS_RET_ERR_EXHAUSTED; }
      ++code;
    }

    if ((byte == 0) || (code == 0xFF)) {
      dst[dst_code_idx] = code;
      dst_code_idx = dst_idx;
      code = 1;

      if ((byte == 0) || dec_len) {
        if (++dst_idx >= enc_max) { return COBS_RET_ERR_EXHAUSTED; }
      }
    }
    ++src_idx;
  }

  dst[dst_code_idx] = code;
  dst[dst_idx++] = COBS_FRAME_DELIMETER;
  *out_enc_len = dst_idx;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_decode(void const *enc,
                       unsigned enc_len,
                       void *out_dec,
                       unsigned dec_max,
                       unsigned *out_dec_len) {
  if (!enc || !out_dec || !out_dec_len) { return COBS_RET_ERR_BAD_ARG; }
  if (enc_len < 2) { return COBS_RET_ERR_BAD_ARG; }

  cobs_byte_t const *const src = (cobs_byte_t const *)enc;
  cobs_byte_t *const dst = (cobs_byte_t *)out_dec;

  if ((src[0] == COBS_FRAME_DELIMETER) || (src[enc_len - 1] != COBS_FRAME_DELIMETER)) {
    return COBS_RET_ERR_BAD_PAYLOAD;
  }

  unsigned src_idx = 0, dst_idx = 0;

  while (src_idx < (enc_len - 1)) {
    unsigned const code = src[src_idx++];
    if (!code) { return COBS_RET_ERR_BAD_PAYLOAD; }
    if ((src_idx + code) > enc_len) { return COBS_RET_ERR_BAD_PAYLOAD; }

    if ((dst_idx + code - 1) > dec_max) { return COBS_RET_ERR_EXHAUSTED; }
    for (unsigned i = 0; i < code - 1; ++i) {
      dst[dst_idx++] = src[src_idx++];
    }

    if ((src_idx < (enc_len - 1)) && (code < 0xFF)) {
      if (dst_idx >= dec_max) { return COBS_RET_ERR_EXHAUSTED; }
      dst[dst_idx++] = 0;
    }
  }

  *out_dec_len = dst_idx;
  return COBS_RET_SUCCESS;
}
