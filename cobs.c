#include "cobs.h"

cobs_ret_t cobs_encode_inplace(void *buf, unsigned len) {
  if (!buf || (len < 2)) {
    return COBS_RET_ERR_BAD_ARG;
  }

  unsigned char * const src = (unsigned char *)buf;
  unsigned cur = 0;
  if ((src[cur] != COBS_INPLACE_SENTINEL_VALUE) ||
      (src[len - 1] != COBS_INPLACE_SENTINEL_VALUE)) {
    return COBS_RET_ERR_BAD_PAYLOAD;
  }

  unsigned patch = cur++;
  while (cur != len - 1) {
    if (src[cur] == 0) {
      if (cur - patch >= 256) {
        return COBS_RET_ERR_BAD_PAYLOAD;
      }
      src[patch] = (unsigned char)(cur - patch);
      patch = cur;
    }
    ++cur;
  }

  if (cur - patch >= 256) {
    return COBS_RET_ERR_BAD_PAYLOAD;
  }

  src[patch] = (unsigned char)(cur - patch);
  src[cur] = 0;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_decode_inplace(void *buf, unsigned len) {
  if (!buf || (len < 2)) {
    return COBS_RET_ERR_BAD_ARG;
  }

  unsigned char * const src = (unsigned char *)buf;
  unsigned cur = 0;
  if ((src[cur] == 0) || (src[len - 1] != 0)) {
    return COBS_RET_ERR_BAD_PAYLOAD;
  }

  while (src[cur]) {
    unsigned const ofs = src[cur];
    src[cur] = 0;
    cur += ofs;
    if (cur >= len) {
      return COBS_RET_ERR_BAD_PAYLOAD;
    }
  }

  src[0] = COBS_INPLACE_SENTINEL_VALUE;
  src[cur] = COBS_INPLACE_SENTINEL_VALUE;
  return COBS_RET_SUCCESS;
}
