#include "cobs.h"

cobs_ret_t cobs_encode(void *buf, size_t len) {
  if (!buf || (len < 2) || (len > 257)) {
    return COBS_RET_ERR_BAD_ARG;
  }

  unsigned char *src = (unsigned char *)buf, *last = src + len - 1;
  if ((*src != COBS_SENTINEL_VALUE) || (*last != COBS_SENTINEL_VALUE)) {
    return COBS_RET_ERR_BAD_PAYLOAD;
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

cobs_ret_t cobs_decode(void *buf, size_t len) {
  if (!buf || (len < 2) || (len > 257)) {
    return COBS_RET_ERR_BAD_ARG;
  }

  unsigned char *src = (unsigned char *)buf, *cur = src, *last = src + len - 1;
  if (*last || !*src) {
    return COBS_RET_ERR_BAD_PAYLOAD;
  }

  while (*cur) {
    unsigned const offset = *cur;
    *cur = 0;
    cur += offset;
  }

  *src = COBS_SENTINEL_VALUE;
  *cur = COBS_SENTINEL_VALUE;
  return COBS_RET_SUCCESS;
}
