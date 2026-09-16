// The scalar half of the differential tests: cobs.c compiled a second time with
// SWAR off and every public symbol renamed, so both can link into one binary.
//
// The renames are in front of the #include so that cobs.h's declarations are
// rewritten along with cobs.c's definitions; otherwise -Wmissing-prototypes fires
// on every function. Nothing here leaks into the shipping header.

#undef COBS_SWAR_WORD_BITS
#define COBS_SWAR_WORD_BITS 8

#define cobs_encode_tinyframe cobs_ref_encode_tinyframe
#define cobs_decode_tinyframe cobs_ref_decode_tinyframe
#define cobs_encode cobs_ref_encode
#define cobs_decode cobs_ref_decode
#define cobs_encode_inc_begin cobs_ref_encode_inc_begin
#define cobs_encode_inc cobs_ref_encode_inc
#define cobs_encode_inc_end cobs_ref_encode_inc_end
#define cobs_decode_inc_begin cobs_ref_decode_inc_begin
#define cobs_decode_inc cobs_ref_decode_inc

#include "../cobs.c"
