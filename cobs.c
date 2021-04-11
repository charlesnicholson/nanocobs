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
                       void *enc,
                       unsigned enc_max,
                       unsigned *out_enc_len) {
  if (!dec || !enc || !out_enc_len || (enc_max < 2) || (enc_max < dec_len)) {
    return COBS_RET_ERR_BAD_ARG;
  }
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_decode(void const *enc,
                       unsigned enc_len,
                       void *dec,
                       unsigned dec_max,
                       unsigned *out_dec_len) {
  if (!enc || !dec || !out_dec_len || (enc_len < 2) || (dec_max < enc_len)) {
    return COBS_RET_ERR_BAD_ARG;
  }
  return COBS_RET_SUCCESS;
}
