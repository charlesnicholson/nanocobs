#pragma once

#include <stddef.h>

typedef enum {
  COBS_RET_SUCCESS = 0,
  COBS_RET_ERR_BAD_ARG,
  COBS_RET_ERR_BAD_PAYLOAD
} cobs_ret_t;

enum {
  COBS_SENTINEL_VALUE = 0x5A
};

#ifdef __cplusplus
extern "C" {
#endif

cobs_ret_t cobs_encode(void *buf, size_t len);
cobs_ret_t cobs_decode(void *buf, size_t len);

#ifdef __cplusplus
}
#endif
