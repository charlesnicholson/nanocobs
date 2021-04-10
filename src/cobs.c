#include "cobs.h"

cobs_ret_t cobs_encode(void *buf, size_t buf_len, size_t *out_enc_len) {
  if (!buf || !out_enc_len || (buf_len < 2)) {
    return COBS_RET_ERR_BAD_ARG;
  }

  unsigned char *src = (unsigned char *)buf;
  if ((src[0] != COBS_ENCODE_SENTINEL_VALUE) ||
      (src[buf_len-1] != COBS_ENCODE_SENTINEL_VALUE)) {
    return COBS_RET_ERR_BAD_SENTINELS;
  }

  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_decode(void *buf, void *out_dec_buf, size_t *out_dec_len) {
  (void)buf;
  (void)out_dec_buf;
  (void)out_dec_len;
  return COBS_RET_SUCCESS;
}
