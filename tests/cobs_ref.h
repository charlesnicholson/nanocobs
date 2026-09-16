#pragma once

#include "../cobs.h"

#ifdef __cplusplus
#include <type_traits>
extern "C" {
#endif

// The symbols cobs_ref.c defines. Declared by hand rather than by including cobs.h
// a second time, and pinned to the real signatures by the static_asserts below.
cobs_ret_t cobs_ref_encode_tinyframe(void* buf, size_t len);
cobs_ret_t cobs_ref_decode_tinyframe(void* buf, size_t len);

cobs_ret_t cobs_ref_encode(void const* dec,
                           size_t dec_len,
                           void* out_enc,
                           size_t enc_max,
                           size_t* out_enc_len);

cobs_ret_t cobs_ref_decode(void const* enc,
                           size_t enc_len,
                           void* out_dec,
                           size_t dec_max,
                           size_t* out_dec_len);

cobs_ret_t cobs_ref_encode_inc_begin(cobs_enc_ctx_t* ctx, void* buf, size_t buf_max);

cobs_ret_t cobs_ref_encode_inc(cobs_enc_ctx_t* ctx,
                               cobs_encode_inc_args_t const* args,
                               size_t* out_dec_src_len,
                               size_t* out_enc_dst_len);

cobs_ret_t cobs_ref_encode_inc_end(cobs_enc_ctx_t* ctx,
                                   void* enc_dst,
                                   size_t enc_dst_max,
                                   size_t* out_enc_dst_len,
                                   bool* out_finished);

cobs_ret_t cobs_ref_decode_inc_begin(cobs_decode_inc_ctx_t* ctx);

cobs_ret_t cobs_ref_decode_inc(cobs_decode_inc_ctx_t* ctx,
                               cobs_decode_inc_args_t const* args,
                               size_t* out_enc_src_len,
                               size_t* out_dec_dst_len,
                               bool* out_decode_complete);
#ifdef __cplusplus
}

static_assert(std::is_same_v<decltype(&cobs_encode), decltype(&cobs_ref_encode)>);
static_assert(std::is_same_v<decltype(&cobs_decode), decltype(&cobs_ref_decode)>);
static_assert(std::is_same_v<decltype(&cobs_encode_tinyframe),
                             decltype(&cobs_ref_encode_tinyframe)>);
static_assert(std::is_same_v<decltype(&cobs_decode_tinyframe),
                             decltype(&cobs_ref_decode_tinyframe)>);
static_assert(std::is_same_v<decltype(&cobs_encode_inc_begin),
                             decltype(&cobs_ref_encode_inc_begin)>);
static_assert(std::is_same_v<decltype(&cobs_encode_inc), decltype(&cobs_ref_encode_inc)>);
static_assert(
    std::is_same_v<decltype(&cobs_encode_inc_end), decltype(&cobs_ref_encode_inc_end)>);
static_assert(std::is_same_v<decltype(&cobs_decode_inc_begin),
                             decltype(&cobs_ref_decode_inc_begin)>);
static_assert(std::is_same_v<decltype(&cobs_decode_inc), decltype(&cobs_ref_decode_inc)>);
#endif
