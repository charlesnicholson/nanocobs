#pragma once

typedef enum {
  COBS_RET_SUCCESS = 0,
  COBS_RET_ERR_BAD_ARG,
  COBS_RET_ERR_BAD_PAYLOAD,
  COBS_RET_ERR_EXHAUSTED
} cobs_ret_t;

enum {
  COBS_INPLACE_SENTINEL_VALUE = 0x5A,
  COBS_INPLACE_SAFE_BUFFER_SIZE = 256
};

#ifdef __cplusplus
extern "C" {
#endif

cobs_ret_t cobs_encode_inplace(void *buf, unsigned len);
cobs_ret_t cobs_decode_inplace(void *buf, unsigned len);

cobs_ret_t cobs_encode(void const *dec,
                       unsigned dec_len,
                       void *out_enc,
                       unsigned enc_max,
                       unsigned *out_enc_len);

cobs_ret_t cobs_decode(void const *enc,
                       unsigned enc_len,
                       void *out_dec,
                       unsigned dec_max,
                       unsigned *out_dec_len);

#ifdef __cplusplus
}
#endif
