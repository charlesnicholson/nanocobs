#pragma once

#include <stddef.h>

typedef enum {
  COBS_RET_SUCCESS = 0,
  COBS_RET_ERR_BAD_ARG,
  COBS_RET_ERR_BAD_SENTINELS,
} cobs_ret_t;

enum {
  COBS_ENCODE_SENTINEL_VALUE = 0x5A,
};

#ifdef __cplusplus
extern "C" {
#endif

cobs_ret_t cobs_encode(void *buf, size_t buf_len, size_t *out_enc_len);
cobs_ret_t cobs_decode(void *buf, void *out_dec_buf, size_t *out_dec_len);

#ifdef __cplusplus
}
#endif
