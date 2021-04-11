#pragma once

typedef enum {
  COBS_RET_SUCCESS = 0,
  COBS_RET_ERR_BAD_ARG,
  COBS_RET_ERR_BAD_PAYLOAD
} cobs_ret_t;

enum {
  COBS_SENTINEL_VALUE = 0x5A,
  COBS_SAFE_BUFFER_SIZE = 256
};

#ifdef __cplusplus
extern "C" {
#endif

cobs_ret_t cobs_encode(void *buf, unsigned len);
cobs_ret_t cobs_decode(void *buf, unsigned len);

#ifdef __cplusplus
}
#endif
