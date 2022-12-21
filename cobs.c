#include "cobs.h"

#define COBS_ISV COBS_INPLACE_SENTINEL_VALUE

typedef unsigned char cobs_byte_t;

cobs_ret_t cobs_encode_inplace(void *buf, unsigned len) {
  if (!buf || (len < 2)) { return COBS_RET_ERR_BAD_ARG; }

  cobs_byte_t *const src = (cobs_byte_t *)buf;
  if ((src[0] != COBS_ISV) || (src[len - 1] != COBS_ISV)) {
    return COBS_RET_ERR_BAD_PAYLOAD;
  }

  unsigned patch = 0, cur = 1;
  while (cur < len - 1) {
    if (src[cur] == COBS_FRAME_DELIMITER) {
      unsigned const ofs = cur - patch;
      if (ofs > 255) { return COBS_RET_ERR_BAD_PAYLOAD; }
      src[patch] = (cobs_byte_t)ofs;
      patch = cur;
    }
    ++cur;
  }
  unsigned const ofs = cur - patch;
  if (ofs > 255) { return COBS_RET_ERR_BAD_PAYLOAD; }
  src[patch] = (cobs_byte_t)ofs;
  src[cur] = 0;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_decode_inplace(void *buf, unsigned const len) {
  if (!buf || (len < 2)) { return COBS_RET_ERR_BAD_ARG; }

  cobs_byte_t *const src = (cobs_byte_t *)buf;
  unsigned ofs, cur = 0;
  while (cur < len && ((ofs = src[cur]) != COBS_FRAME_DELIMITER)) {
    src[cur] = 0;
    for (unsigned i = 1; i < ofs; ++i) {
      if (src[cur + i] == 0) { return COBS_RET_ERR_BAD_PAYLOAD; }
    }
    cur += ofs;
  }

  if (cur != len - 1) { return COBS_RET_ERR_BAD_PAYLOAD; }
  src[0] = COBS_ISV;
  src[len - 1] = COBS_ISV;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_encode(void const *dec,
                       unsigned dec_len,
                       void *out_enc,
                       unsigned enc_max,
                       unsigned *out_enc_len) {
  if (!out_enc_len) { return COBS_RET_ERR_BAD_ARG; }

  cobs_enc_ctx_t ctx;
  cobs_ret_t r;
  r = cobs_encode_inc_begin(out_enc, enc_max, &ctx);
  if (r != COBS_RET_SUCCESS) { return r; }
  r = cobs_encode_inc(&ctx, dec, dec_len);
  if (r != COBS_RET_SUCCESS) { return r; }
  r = cobs_encode_inc_end(&ctx, out_enc_len);
  return r;
}

cobs_ret_t cobs_encode_inc_begin(void *out_enc,
                                 unsigned enc_max,
                                 cobs_enc_ctx_t *out_ctx) {
  if (!out_enc || !out_ctx) { return COBS_RET_ERR_BAD_ARG; }
  if (enc_max < 2) { return COBS_RET_ERR_BAD_ARG; }

  out_ctx->dst = out_enc;
  out_ctx->dst_max = enc_max;
  out_ctx->cur = 1;
  out_ctx->code = 1;
  out_ctx->code_idx = 0;
  out_ctx->need_advance = 0;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_encode_inc(cobs_enc_ctx_t *ctx,
                           void const *dec,
                           unsigned dec_len) {
  if (!ctx || !dec) { return COBS_RET_ERR_BAD_ARG; }
  unsigned dst_idx = ctx->cur;
  unsigned const enc_max = ctx->dst_max;
  if ((enc_max - dst_idx) < dec_len) { return COBS_RET_ERR_EXHAUSTED; }
  if (!dec_len) { return COBS_RET_SUCCESS; }

  unsigned dst_code_idx = ctx->code_idx;
  unsigned code = ctx->code;
  int need_advance = ctx->need_advance;

  cobs_byte_t const *const src = (cobs_byte_t const *)dec;
  cobs_byte_t *const dst = (cobs_byte_t *)ctx->dst;
  unsigned src_idx = 0;

  if (need_advance) {
    if (++dst_idx >= enc_max) { return COBS_RET_ERR_EXHAUSTED; }
    need_advance = 0;
  }

  while (dec_len--) {
    cobs_byte_t const byte = src[src_idx];
    if (byte) {
      dst[dst_idx] = byte;
      if (++dst_idx >= enc_max) { return COBS_RET_ERR_EXHAUSTED; }
      ++code;
    }

    if ((byte == 0) || (code == 0xFF)) {
      dst[dst_code_idx] = (cobs_byte_t)code;
      dst_code_idx = dst_idx;
      code = 1;

      if ((byte == 0) || dec_len) {
        if (++dst_idx >= enc_max) { return COBS_RET_ERR_EXHAUSTED; }
      } else {
        need_advance = !dec_len;
      }
    }
    ++src_idx;
  }

  ctx->cur = dst_idx;
  ctx->code = code;
  ctx->code_idx = dst_code_idx;
  ctx->need_advance = need_advance;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_encode_inc_end(cobs_enc_ctx_t *ctx, unsigned *out_enc_len) {
  if (!ctx || !out_enc_len) { return COBS_RET_ERR_BAD_ARG; }

  cobs_byte_t *const dst = (cobs_byte_t *)ctx->dst;
  unsigned cur = ctx->cur;
  dst[ctx->code_idx] = (cobs_byte_t)ctx->code;
  dst[cur++] = COBS_FRAME_DELIMITER;
  *out_enc_len = cur;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_decode(void const *enc,
                       unsigned enc_len,
                       void *out_dec,
                       unsigned dec_max,
                       unsigned *out_dec_len) {
  if (!enc || !out_dec || !out_dec_len) { return COBS_RET_ERR_BAD_ARG; }
  if (enc_len < 2) { return COBS_RET_ERR_BAD_ARG; }

  cobs_byte_t const *const src = (cobs_byte_t const *)enc;
  cobs_byte_t *const dst = (cobs_byte_t *)out_dec;

  if ((src[0] == COBS_FRAME_DELIMITER) || (src[enc_len - 1] != COBS_FRAME_DELIMITER)) {
    return COBS_RET_ERR_BAD_PAYLOAD;
  }

  unsigned src_idx = 0, dst_idx = 0;

  while (src_idx < (enc_len - 1)) {
    unsigned const code = src[src_idx++];
    if (!code) { return COBS_RET_ERR_BAD_PAYLOAD; }
    if ((src_idx + code) > enc_len) { return COBS_RET_ERR_BAD_PAYLOAD; }

    if ((dst_idx + code - 1) > dec_max) { return COBS_RET_ERR_EXHAUSTED; }
    for (unsigned i = 0; i < code - 1; ++i) {
      if (src[src_idx] == 0) { return COBS_RET_ERR_BAD_PAYLOAD; }
      dst[dst_idx++] = src[src_idx++];
    }

    if ((src_idx < (enc_len - 1)) && (code < 0xFF)) {
      if (dst_idx >= dec_max) { return COBS_RET_ERR_EXHAUSTED; }
      dst[dst_idx++] = 0;
    }
  }

  *out_dec_len = dst_idx;
  return COBS_RET_SUCCESS;
}
