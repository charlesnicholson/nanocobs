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
    if (!src[cur]) {
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
  while ((ofs = src[cur]) != 0) {
    src[cur] = 0;
    cur += ofs;
    if (cur > len) { return COBS_RET_ERR_BAD_PAYLOAD; }
  }

  if (cur != len - 1) { return COBS_RET_ERR_BAD_PAYLOAD; }
  src[0] = COBS_ISV;
  src[len - 1] = COBS_ISV;
  return COBS_RET_SUCCESS;
}

unsigned cobs_encode_max(unsigned dec_len) {
  return 1 + dec_len + ((dec_len + 253) / 254) + (dec_len == 0);
}

cobs_ret_t cobs_encode(void const *dec,
                       unsigned dec_len,
                       void *out_enc,
                       unsigned enc_max,
                       unsigned *out_enc_len) {
  if (!dec || !out_enc || !out_enc_len) { return COBS_RET_ERR_BAD_ARG; }
  if ((enc_max < 2) || (enc_max < dec_len)) { return COBS_RET_ERR_BAD_ARG; }

  cobs_byte_t const *src = (cobs_byte_t const *)dec;
  cobs_byte_t *dst = (cobs_byte_t *)out_enc;
  cobs_byte_t const *const dst_max = dst + enc_max;
  cobs_byte_t *code_dst = dst++;
  cobs_byte_t code = 1;

  while (dec_len--) {
    cobs_byte_t const byte = *src;
    if (byte) {
      *dst = byte;
      if (++dst >= dst_max) { return COBS_RET_ERR_EXHAUSTED; }
      ++code;
    }

    if ((byte == 0) || (code == 0xFF)) {
      *code_dst = code;
      code_dst = dst;
      code = 1;

      if ((byte == 0) || dec_len) {
        if (++dst >= dst_max) { return COBS_RET_ERR_EXHAUSTED; }
      }
    }
    ++src;
  }

  *code_dst = code;
  *dst++ = 0;
  *out_enc_len = (unsigned)(dst - (cobs_byte_t *)out_enc);
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_decode(void const *enc,
                       unsigned enc_len,
                       void *out_dec,
                       unsigned dec_max,
                       unsigned *out_dec_len) {
  if (!enc || !out_dec || !out_dec_len) { return COBS_RET_ERR_BAD_ARG; }
  if (enc_len < 2) { return COBS_RET_ERR_BAD_ARG; }

  cobs_byte_t const *src = (cobs_byte_t const *)enc;
  cobs_byte_t const *const end = src + enc_len - 1;
  if (!*src || *end) { return COBS_RET_ERR_BAD_PAYLOAD; }

  cobs_byte_t *dst = (cobs_byte_t *)out_dec;
  unsigned dec_len = 0;

  while (src < end) {
    unsigned const code = *src++;
    if (!code) { return COBS_RET_ERR_BAD_PAYLOAD; }
    if (src + code - 1 > end) { return COBS_RET_ERR_BAD_PAYLOAD; }

    dec_len += code - 1;
    if (dec_len > dec_max) { return COBS_RET_ERR_EXHAUSTED; }
    for (unsigned i = 0; i < code - 1; ++i) {
      *dst++ = *src++;
    }

    if ((src < end) && (code < 0xFF)) {
      if (++dec_len > dec_max) { return COBS_RET_ERR_EXHAUSTED; }
      *dst++ = 0;
    }
  }

  *out_dec_len = dec_len;
  return COBS_RET_SUCCESS;
}
