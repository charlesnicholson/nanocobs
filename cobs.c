// SPDX-License-Identifier: Unlicense OR 0BSD
#include "cobs.h"

#define COBS_TFSV COBS_TINYFRAME_SENTINEL_VALUE

cobs_ret_t cobs_encode_tinyframe(void* buf, size_t len) {
  if (!buf || (len < 2)) {
    return COBS_RET_ERR_BAD_ARG;
  }

  cobs_byte_t* const src = (cobs_byte_t*)buf;
  if ((src[0] != COBS_TFSV) || (src[len - 1] != COBS_TFSV)) {
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

cobs_ret_t cobs_decode_tinyframe(void* buf, size_t const len) {
  if (!buf || (len < 2)) {
    return COBS_RET_ERR_BAD_ARG;
  }

  cobs_byte_t* const src = (cobs_byte_t*)buf;
  size_t ofs, cur = 0;
  while ((cur < len) && ((ofs = src[cur]) != COBS_FRAME_DELIMITER)) {
    src[cur] = 0;
    if (cur + ofs > len) {
      return COBS_RET_ERR_BAD_PAYLOAD;
    }
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
  src[0] = COBS_TFSV;
  src[len - 1] = COBS_TFSV;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_encode(void const* dec,
                       size_t dec_len,
                       void* out_enc,
                       size_t enc_max,
                       size_t* out_enc_len) {
  if (!dec || !out_enc || !out_enc_len) {
    return COBS_RET_ERR_BAD_ARG;
  }
  if (enc_max < 2) {
    return COBS_RET_ERR_BAD_ARG;
  }

  cobs_byte_t const* const src = (cobs_byte_t const*)dec;
  cobs_byte_t* const dst = (cobs_byte_t*)out_enc;

  size_t src_idx = 0;
  size_t dst_idx = 1;
  size_t code_idx = 0;
  unsigned code = 1;

  while (src_idx < dec_len) {
    if (dst_idx >= enc_max) {
      return COBS_RET_ERR_EXHAUSTED;
    }

    cobs_byte_t const byte = src[src_idx];
    if (byte) {
      dst[dst_idx] = byte;
      ++dst_idx;
      ++code;
    }

    if ((byte == 0) || (code == 0xFF)) {
      dst[code_idx] = (cobs_byte_t)code;
      code_idx = dst_idx;
      code = 1;

      // Advance past code placeholder unless this is the last source byte
      // AND code==0xFF. In that case, code_idx == dst_idx, and the final
      // code byte will be overwritten by the delimiter (correct COBS).
      if ((byte == 0) || (src_idx + 1 < dec_len)) {
        ++dst_idx;
      }
    }
    ++src_idx;
  }

  dst[code_idx] = (cobs_byte_t)code;
  if (dst_idx >= enc_max) {
    return COBS_RET_ERR_EXHAUSTED;
  }
  dst[dst_idx++] = COBS_FRAME_DELIMITER;
  *out_enc_len = dst_idx;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_encode_inc_begin(cobs_enc_ctx_t* ctx, void* buf, size_t buf_max) {
  if (!ctx || !buf) {
    return COBS_RET_ERR_BAD_ARG;
  }
  if (buf_max < 255) {
    return COBS_RET_ERR_BAD_ARG;
  }

  ctx->state = COBS_ENCODE_ACCUMULATE;
  ctx->buf = (cobs_byte_t*)buf;
  ctx->code = 1;
  ctx->buf_len = 1;
  ctx->flush_pos = 0;
  ctx->prev_was_ff = 0;
  return COBS_RET_SUCCESS;
}

static inline size_t flush_block(cobs_enc_ctx_t* ctx, cobs_byte_t* dst, size_t dst_max) {
  unsigned pos = ctx->flush_pos;
  unsigned const len = ctx->buf_len;
  cobs_byte_t const* const buf = ctx->buf;
  size_t written = 0;
  while ((pos < len) && (written < dst_max)) {
    dst[written++] = buf[pos++];
  }
  ctx->flush_pos = (uint8_t)pos;
  return written;
}

cobs_ret_t cobs_encode_inc(cobs_enc_ctx_t* ctx,
                           cobs_encode_inc_args_t const* args,
                           size_t* out_dec_src_len,
                           size_t* out_enc_dst_len) {
  if (!ctx || !args || !out_dec_src_len || !out_enc_dst_len || !args->dec_src ||
      !args->enc_dst) {
    return COBS_RET_ERR_BAD_ARG;
  }

  if (ctx->state >= COBS_ENCODE_FLUSH_FINAL) {
    return COBS_RET_ERR_BAD_ARG;
  }

  cobs_byte_t const* const src = (cobs_byte_t const*)args->dec_src;
  cobs_byte_t* const dst = (cobs_byte_t*)args->enc_dst;
  size_t const src_max = args->dec_src_max;
  size_t const dst_max = args->enc_dst_max;
  size_t src_idx = 0;
  size_t dst_idx = 0;

  cobs_byte_t* const buf = ctx->buf;
  unsigned code = ctx->code;
  unsigned buf_len = ctx->buf_len;
  enum cobs_encode_inc_state state = ctx->state;

  for (;;) {
    if (state == COBS_ENCODE_FLUSHING) {
      ctx->buf_len = (uint8_t)buf_len;
      dst_idx += flush_block(ctx, dst + dst_idx, dst_max - dst_idx);
      if (ctx->flush_pos < buf_len) {
        goto done;
      }
      state = COBS_ENCODE_ACCUMULATE;
      code = 1;
      buf_len = 1;
      ctx->flush_pos = 0;
    }

    if (src_idx >= src_max) {
      goto done;
    }

    cobs_byte_t const byte = src[src_idx];
    if (byte) {
      buf[buf_len++] = byte;
      ++code;
    }

    if ((byte == 0) || (code == 0xFF)) {
      ctx->prev_was_ff = (code == 0xFF);
      buf[0] = (cobs_byte_t)code;
      ctx->flush_pos = 0;
      state = COBS_ENCODE_FLUSHING;
    }

    ++src_idx;
  }

done:
  ctx->state = state;
  ctx->code = (uint8_t)code;
  ctx->buf_len = (uint8_t)buf_len;
  *out_dec_src_len = src_idx;
  *out_enc_dst_len = dst_idx;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_encode_inc_end(cobs_enc_ctx_t* ctx,
                               void* enc_dst,
                               size_t enc_dst_max,
                               size_t* out_enc_dst_len,
                               bool* out_finished) {
  if (!ctx || !enc_dst || !out_enc_dst_len || !out_finished) {
    return COBS_RET_ERR_BAD_ARG;
  }

  cobs_byte_t* const dst = (cobs_byte_t*)enc_dst;
  size_t dst_idx = 0;
  enum cobs_encode_inc_state state = ctx->state;

  for (;;) {
    switch (state) {
      case COBS_ENCODE_FLUSHING: {
        dst_idx += flush_block(ctx, dst + dst_idx, enc_dst_max - dst_idx);
        if (ctx->flush_pos < ctx->buf_len) {
          goto done;
        }
        state = COBS_ENCODE_ACCUMULATE;
        ctx->code = 1;
        ctx->buf_len = 1;
        ctx->flush_pos = 0;
      } break;

      case COBS_ENCODE_ACCUMULATE: {
        // If the previous block was 0xFF and no new data accumulated (code==1,
        // buf_len==1), skip the redundant trailing code byte — the delimiter
        // directly follows the 0xFF block, matching standalone cobs_encode().
        if (ctx->prev_was_ff && (ctx->code == 1) && (ctx->buf_len == 1)) {
          state = COBS_ENCODE_WRITE_DELIM;
        } else {
          ctx->buf[0] = (cobs_byte_t)ctx->code;
          ctx->flush_pos = 0;
          state = COBS_ENCODE_FLUSH_FINAL;
        }
      } break;

      case COBS_ENCODE_FLUSH_FINAL: {
        dst_idx += flush_block(ctx, dst + dst_idx, enc_dst_max - dst_idx);
        if (ctx->flush_pos < ctx->buf_len) {
          goto done;
        }
        state = COBS_ENCODE_WRITE_DELIM;
      } break;

      case COBS_ENCODE_WRITE_DELIM: {
        if (dst_idx >= enc_dst_max) {
          goto done;
        }
        dst[dst_idx++] = COBS_FRAME_DELIMITER;
        state = COBS_ENCODE_DONE;
      } break;

      case COBS_ENCODE_DONE: {
        goto done;
      }
    }
  }

done:
  ctx->state = state;
  *out_enc_dst_len = dst_idx;
  *out_finished = (state == COBS_ENCODE_DONE);
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_decode(void const* enc,
                       size_t enc_len,
                       void* out_dec,
                       size_t dec_max,
                       size_t* out_dec_len) {
  if (!enc || !out_dec || !out_dec_len) {
    return COBS_RET_ERR_BAD_ARG;
  }
  if (enc_len < 2) {
    return COBS_RET_ERR_BAD_ARG;
  }

  cobs_decode_inc_ctx_t ctx;
  cobs_ret_t r = cobs_decode_inc_begin(&ctx);
  if (r != COBS_RET_SUCCESS) {
    return r;
  }

  size_t src_len;
  bool decode_complete;
  if ((r = cobs_decode_inc(&ctx,
                           &(cobs_decode_inc_args_t){ .enc_src = enc,
                                                      .dec_dst = out_dec,
                                                      .enc_src_max = enc_len,
                                                      .dec_dst_max = dec_max },
                           &src_len,
                           out_dec_len,
                           &decode_complete)) != COBS_RET_SUCCESS) {
    return r;
  }
  return decode_complete ? COBS_RET_SUCCESS : COBS_RET_ERR_EXHAUSTED;
}

cobs_ret_t cobs_decode_inc_begin(cobs_decode_inc_ctx_t* ctx) {
  if (!ctx) {
    return COBS_RET_ERR_BAD_ARG;
  }
  ctx->state = COBS_DECODE_READ_CODE;
  return COBS_RET_SUCCESS;
}

cobs_ret_t cobs_decode_inc(cobs_decode_inc_ctx_t* ctx,
                           cobs_decode_inc_args_t const* args,
                           size_t* out_enc_src_len,
                           size_t* out_dec_dst_len,
                           bool* out_decode_complete) {
  if (!ctx || !args || !out_enc_src_len || !out_dec_dst_len || !out_decode_complete ||
      !args->dec_dst || !args->enc_src) {
    return COBS_RET_ERR_BAD_ARG;
  }

  bool decode_complete = false;
  size_t src_idx = 0, dst_idx = 0;

  size_t const src_max = args->enc_src_max;
  size_t const dst_max = args->dec_dst_max;
  cobs_byte_t const* src_b = (cobs_byte_t const*)args->enc_src;
  cobs_byte_t* dst_b = (cobs_byte_t*)args->dec_dst;
  unsigned block = ctx->block, code = ctx->code;
  enum cobs_decode_inc_state state = ctx->state;

  while (src_idx < src_max) {
    switch (state) {
      case COBS_DECODE_READ_CODE: {
        block = code = src_b[src_idx++];
        state = COBS_DECODE_RUN;
      } break;

      case COBS_DECODE_FINISH_RUN: {
        if (!src_b[src_idx]) {
          decode_complete = true;
          goto done;
        }

        if (code != 0xFF) {
          if (dst_idx >= dst_max) {
            goto done;
          }
          dst_b[dst_idx++] = 0;
        }
        state = COBS_DECODE_READ_CODE;
      } break;

      case COBS_DECODE_RUN: {
        while (block - 1) {
          if ((src_idx >= src_max) || (dst_idx >= dst_max)) {
            goto done;
          }

          --block;
          cobs_byte_t const b = src_b[src_idx++];
          if (!b) {
            return COBS_RET_ERR_BAD_PAYLOAD;
          }

          dst_b[dst_idx++] = b;
        }
        state = COBS_DECODE_FINISH_RUN;
      } break;
    }
  }

done:
  ctx->state = state;
  ctx->code = (uint8_t)code;
  ctx->block = (uint8_t)block;
  *out_dec_dst_len = dst_idx;
  *out_enc_src_len = src_idx;
  *out_decode_complete = decode_complete;
  return COBS_RET_SUCCESS;
}
