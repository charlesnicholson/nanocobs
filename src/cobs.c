#include "cobs.h"

cobs_ret_t cobs_encode(void *buf, size_t buf_len) {
  if (!buf || (buf_len < 2) || (buf_len > 257)) {
    return COBS_RET_ERR_BAD_ARG;
  }

  unsigned char *src = (unsigned char *)buf;
  unsigned char *last = src + buf_len - 1;

  if ((*src != COBS_SENTINEL_VALUE) || (*last != COBS_SENTINEL_VALUE)) {
    return COBS_RET_ERR_BAD_SENTINELS;
  }

  unsigned char *patch = src++;
  while (src != last) {
    if (!*src) {
      *patch = (unsigned char)(src - patch);
      patch = src;
    }
    ++src;
  }

  *patch = (unsigned char)(src - patch);
  *last = 0;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_decode(void *buf, void *out_dec_buf, size_t *out_dec_len) {
  (void)buf;
  (void)out_dec_buf;
  (void)out_dec_len;
  return COBS_RET_SUCCESS;
}
