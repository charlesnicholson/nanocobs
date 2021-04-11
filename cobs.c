#include "cobs.h"

#define COBS_ISV COBS_INPLACE_SENTINEL_VALUE
typedef unsigned char cobs_byte_t;

cobs_ret_t cobs_encode_inplace(void *buf, unsigned len) {
  if (!buf || (len < 2)) {
    return COBS_RET_ERR_BAD_ARG;
  }

  cobs_byte_t *cur = (cobs_byte_t *)buf;
  cobs_byte_t const *const end = cur + len - 1;
  if ((*cur != COBS_ISV) || (*end != COBS_ISV)) {
    return COBS_RET_ERR_BAD_PAYLOAD;
  }

  cobs_byte_t *patch = cur++;
  while (cur != end) {
    if (*cur == 0) {
      unsigned const ofs = (unsigned)(cur - patch);
      if (ofs > 255) {
        return COBS_RET_ERR_BAD_PAYLOAD;
      }
      *patch = (cobs_byte_t)ofs;
      patch = cur;
    }
    ++cur;
  }

  unsigned const ofs = (unsigned)(cur - patch);
  if (ofs >= 256) {
    return COBS_RET_ERR_BAD_PAYLOAD;
  }
  *patch = (cobs_byte_t)ofs;
  *cur = 0;

  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_decode_inplace(void *buf, unsigned len) {
  if (!buf || (len < 2)) {
    return COBS_RET_ERR_BAD_ARG;
  }

  cobs_byte_t *cur = (cobs_byte_t *)buf;
  cobs_byte_t const *const end = cur + len - 1;
  if ((*cur == 0) || (*end != 0)) {
    return COBS_RET_ERR_BAD_PAYLOAD;
  }

  while (*cur) {
    unsigned const ofs = *cur;
    *cur = 0;
    cur += ofs;
    if (cur > end) {
      return COBS_RET_ERR_BAD_PAYLOAD;
    }
  }

  *(cobs_byte_t *)buf = COBS_ISV;
  *cur = COBS_ISV;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_encode(void const *dec,
                       unsigned dec_len,
                       unsigned enc_max,
                       void *out_enc,
                       unsigned *out_enc_len) {
  if (!dec || !out_enc || !out_enc_len) {
    return COBS_RET_ERR_BAD_ARG;
  }
  if ((enc_max < 2) || (enc_max < dec_len)) {
    return COBS_RET_ERR_BAD_ARG;
  }

  cobs_byte_t const *src = (cobs_byte_t const *)dec;
  cobs_byte_t *dst = (cobs_byte_t *)out_enc;
  cobs_byte_t *code_dst = dst++;
  cobs_byte_t code = 1;
  unsigned enc_len = 0;

#define COBS_ENCODE__PUT(NONZERO_BYTE) \
  do { \
    if (++enc_len > enc_max) { return COBS_RET_ERR_EXHAUSTED; } \
    *dst++ = (NONZERO_BYTE); \
  } while (0)

  while (dec_len--) {
    cobs_byte_t byte = *src;
    if ((byte == 0) || (code == 0xFF)) {
      *code_dst = code;
      code_dst = dst;
      code = 1;
    } else {
      COBS_ENCODE__PUT(*src);
      ++code;
    }
    ++src;
  }

  *code_dst = code;
  COBS_ENCODE__PUT(0x00);
#undef COBS_ENCODE__PUT

  *out_enc_len = enc_len;
  return COBS_RET_SUCCESS;
}


cobs_ret_t cobs_decode(void const *enc,
                       unsigned enc_len,
                       unsigned dec_max,
                       void *out_dec,
                       unsigned *out_dec_len) {
  if (!enc || !out_dec || !out_dec_len) {
    return COBS_RET_ERR_BAD_ARG;
  }
  if ((enc_len < 2) || (dec_max < enc_len)) {
    return COBS_RET_ERR_BAD_ARG;
  }
  return COBS_RET_SUCCESS;
}
