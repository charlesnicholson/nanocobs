#include "cobs.h"

#define COBS_ISV COBS_INPLACE_SENTINEL_VALUE

typedef unsigned char cobs_byte_t;

cobs_ret_t cobs_encode_inplace(void *buf, size_t len) {
  if (!buf || (len < 2)) {
    return COBS_RET_ERR_BAD_ARG;
  }

  cobs_byte_t *const src = (cobs_byte_t *)buf;
  if ((src[0] != COBS_ISV) || (src[len - 1] != COBS_ISV)) {
    return COBS_RET_ERR_BAD_PAYLOAD;
  }

  size_t patch = 0, cur = 1;
  while (cur < len - 1) {
    if (src[cur] == COBS_FRAME_DELIMITER) {
      size_t const ofs = cur - patch;
      if (ofs > 255) {
        return COBS_RET_ERR_BAD_PAYLOAD;
      }
      src[patch] = (cobs_byte_t)ofs;
      patch = cur;
    }
    ++cur;
  }
  size_t const ofs = cur - patch;
  if (ofs > 255) {
    return COBS_RET_ERR_BAD_PAYLOAD;
  }
  src[patch] = (cobs_byte_t)ofs;
  src[cur] = 0;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_decode_inplace(void *buf, size_t const len) {
  if (!buf || (len < 2)) {
    return COBS_RET_ERR_BAD_ARG;
  }

  cobs_byte_t *const src = (cobs_byte_t *)buf;
  size_t ofs, cur = 0;
  while ((cur < len) && ((ofs = src[cur]) != COBS_FRAME_DELIMITER)) {
    src[cur] = 0;
    for (size_t i = 1; i < ofs; ++i) {
      if (src[cur + i] == 0) {
        return COBS_RET_ERR_BAD_PAYLOAD;
      }
    }
    cur += ofs;
  }

  if (cur != len - 1) {
    return COBS_RET_ERR_BAD_PAYLOAD;
  }
  src[0] = COBS_ISV;
  src[len - 1] = COBS_ISV;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_encode(void const *dec,
                       size_t dec_len,
                       void *out_enc,
                       size_t enc_max,
                       size_t *out_enc_len) {
  if (!out_enc_len) {
    return COBS_RET_ERR_BAD_ARG;
  }

  cobs_enc_ctx_t ctx;
  cobs_ret_t r;
  if ((r = cobs_encode_inc_begin(out_enc, enc_max, &ctx)) != COBS_RET_SUCCESS) {
    return r;
  }
  if ((r = cobs_encode_inc(&ctx, dec, dec_len)) != COBS_RET_SUCCESS) {
    return r;
  }
  return cobs_encode_inc_end(&ctx, out_enc_len);
}

cobs_ret_t cobs_encode_inc_begin(void *out_enc, size_t enc_max, cobs_enc_ctx_t *out_ctx) {
  if (!out_enc || !out_ctx) {
    return COBS_RET_ERR_BAD_ARG;
  }
  if (enc_max < 2) {
    return COBS_RET_ERR_BAD_ARG;
  }

  out_ctx->dst = out_enc;
  out_ctx->dst_max = enc_max;
  out_ctx->cur = 1;
  out_ctx->code = 1;
  out_ctx->code_idx = 0;
  out_ctx->need_advance = 0;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_encode_inc(cobs_enc_ctx_t *ctx, void const *dec, size_t dec_len) {
  if (!ctx || !dec) {
    return COBS_RET_ERR_BAD_ARG;
  }
  size_t dst_idx = ctx->cur;
  size_t const enc_max = ctx->dst_max;
  if ((enc_max - dst_idx) < dec_len) {
    return COBS_RET_ERR_EXHAUSTED;
  }
  if (!dec_len) {
    return COBS_RET_SUCCESS;
  }

  size_t dst_code_idx = ctx->code_idx;
  unsigned code = ctx->code;
  int need_advance = ctx->need_advance;

  cobs_byte_t const *const src = (cobs_byte_t const *)dec;
  cobs_byte_t *const dst = (cobs_byte_t *)ctx->dst;
  size_t src_idx = 0;

  if (need_advance) {
    if (++dst_idx >= enc_max) {
      return COBS_RET_ERR_EXHAUSTED;
    }
    need_advance = 0;
  }

  while (dec_len--) {
    cobs_byte_t const byte = src[src_idx];
    if (byte) {
      dst[dst_idx] = byte;
      if (++dst_idx >= enc_max) {
        return COBS_RET_ERR_EXHAUSTED;
      }
      ++code;
    }

    if ((byte == 0) || (code == 0xFF)) {
      dst[dst_code_idx] = (cobs_byte_t)code;
      dst_code_idx = dst_idx;
      code = 1;

      if ((byte == 0) || dec_len) {
        if (++dst_idx >= enc_max) {
          return COBS_RET_ERR_EXHAUSTED;
        }
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

cobs_ret_t cobs_encode_inc_end(cobs_enc_ctx_t *ctx, size_t *out_enc_len) {
  if (!ctx || !out_enc_len) {
    return COBS_RET_ERR_BAD_ARG;
  }

  cobs_byte_t *const dst = (cobs_byte_t *)ctx->dst;
  size_t cur = ctx->cur;
  dst[ctx->code_idx] = (cobs_byte_t)ctx->code;
  dst[cur++] = COBS_FRAME_DELIMITER;
  *out_enc_len = cur;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_decode(void const *enc,
                       size_t enc_len,
                       void *out_dec,
                       size_t dec_max,
                       size_t *out_dec_len) {
  if (!enc || !out_dec || !out_dec_len) {
    return COBS_RET_ERR_BAD_ARG;
  }
  if (enc_len < 2) {
    return COBS_RET_ERR_BAD_ARG;
  }

  cobs_decode_args_t args = {
    .src = enc,
    .dst = out_dec,
    .src_max = enc_len,
    .dst_max = dec_max,
    .src_len = 0,
    .dst_len = 0,
    .decode_complete = 0,
  };

  cobs_decode_ctx_t ctx;
  cobs_ret_t r;
  if ((r = cobs_decode_inc_begin(&ctx)) != COBS_RET_SUCCESS) {
    return r;
  }
  if ((r = cobs_decode_inc(&ctx, &args)) != COBS_RET_SUCCESS) {
    return r;
  }
  *out_dec_len = args.dst_len;
  return args.decode_complete ? COBS_RET_SUCCESS : COBS_RET_ERR_EXHAUSTED;
}

cobs_ret_t cobs_decode_inc_begin(cobs_decode_ctx_t *ctx) {
  if (!ctx) {
    return COBS_RET_ERR_BAD_ARG;
  }
  ctx->state = COBS_DECODE_READ_CODE;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_decode_inc(cobs_decode_ctx_t *ctx, cobs_decode_args_t *args) {
  if (!ctx || !args) {
    return COBS_RET_ERR_BAD_ARG;
  }

  size_t src_idx = 0, dst_idx = 0;
  cobs_byte_t const *src_b = (cobs_byte_t const *)args->src;
  cobs_byte_t *dst_b = (cobs_byte_t *)args->dst;

  while (src_idx < args->src_max) {
    switch (ctx->state) {
      case COBS_DECODE_READ_CODE: {
        ctx->block = ctx->code = src_b[src_idx++];
        ctx->state = COBS_DECODE_RUN;
      } break;

      case COBS_DECODE_FINISH_RUN: {
        if (!src_b[src_idx]) {
          args->decode_complete = 1;
          goto done;
        }

        if (ctx->code != 0xFF) {
          if (dst_idx >= args->dst_max) {
            goto done;
          }
          dst_b[dst_idx++] = 0;
        }
        ctx->state = COBS_DECODE_READ_CODE;
      } break;

      case COBS_DECODE_RUN: {
        while (--ctx->block) {
          if ((src_idx >= args->src_max) || (dst_idx >= args->dst_max)) {
            goto done;
          }

          cobs_byte_t const b = src_b[src_idx++];
          if (!b) {
            return COBS_RET_ERR_BAD_PAYLOAD;
          }

          dst_b[dst_idx++] = b;
        }
        ctx->state = COBS_DECODE_FINISH_RUN;
      } break;
    }
  }

done:
  args->dst_len = dst_idx;
  args->src_len = src_idx;
  return COBS_RET_SUCCESS;
}
