/*
  Differential probe for emulated 16- and 32-bit targets, run by run.py.

  Every host that runs the doctest suite has a 64-bit size_t, so nothing there
  exercises COBS_SWAR_WORD_BITS at 16 or 32, nor the integer promotion the SWAR
  predicate goes through when cobs_word_t is narrower than int. This links the
  shipping cobs.c against a byte-loop copy of the same source and compares them.
*/

#include "../../cobs.h"
#include "../cobs_ref.h"

#define PAYLOAD_MAX 12u
#define ENC_MAX (PAYLOAD_MAX + PAYLOAD_MAX / 254u + 2u)

// Freestanding on purpose: a bare cross toolchain without newlib still builds this.
static void pset(unsigned char *p, unsigned char v, unsigned n) {
  while (n--) {
    *p++ = v;
  }
}

static int pcmp(unsigned char const *a, unsigned char const *b, unsigned n) {
  while (n--) {
    if (*a++ != *b++) {
      return 1;
    }
  }
  return 0;
}

void probe_init(void);
void probe_puts(char const *s);
void probe_putu(unsigned long v);
void probe_done(int failed);

static unsigned long passed, failed;

static void fail(char const *tag, unsigned long m, unsigned n) {
  ++failed;
  if (failed <= 8u) {
    probe_puts("FAIL ");
    probe_puts(tag);
    probe_puts(" mask=");
    probe_putu(m);
    probe_puts(" len=");
    probe_putu(n);
    probe_puts("\r\n");
  }
}

static int run(void) {
  unsigned char dec[PAYLOAD_MAX], a[ENC_MAX], b[ENC_MAX], ra[PAYLOAD_MAX + 2],
      rb[PAYLOAD_MAX + 2];

  probe_puts("PROBE START word=");
  probe_putu((unsigned long)(sizeof(size_t) * 8u));
  probe_puts("\r\n");

  for (unsigned n = 0; n <= PAYLOAD_MAX; ++n) {
    probe_puts(".");
    unsigned long const combos = 1uL << n;
    for (unsigned long m = 0; m < combos; ++m) {
      for (unsigned i = 0; i < n; ++i) {
        dec[i] = ((m >> i) & 1uL) ? 0x01u : 0x00u;
      }

      pset(a, 0xCD, sizeof a);
      pset(b, 0xCD, sizeof b);
      size_t la = 0, lb = 0;
      cobs_ret_t const ea = cobs_ref_encode(dec, n, a, sizeof a, &la);
      cobs_ret_t const eb = cobs_encode(dec, n, b, sizeof b, &lb);
#ifdef COBS_PROBE_FAULT
      // Seeded mismatch, so CI can prove a wrong result inside qemu fails the run.
      if ((n == 5u) && (m == 3uL)) {
        b[0] = (unsigned char)(b[0] ^ 0xFFu);
      }
#endif
      if ((ea != eb) || (la != lb) || pcmp(a, b, sizeof a)) {
        fail("encode", m, n);
        continue;
      }
      if (ea != COBS_RET_SUCCESS) {
        fail("encode-ret", m, n);
        continue;
      }

      pset(ra, 0xCD, sizeof ra);
      pset(rb, 0xCD, sizeof rb);
      size_t da = 0, db = 0;
      cobs_ret_t const dra = cobs_ref_decode(a, la, ra, sizeof ra, &da);
      cobs_ret_t const drb = cobs_decode(b, lb, rb, sizeof rb, &db);
      if ((dra != drb) || (da != db) || pcmp(ra, rb, sizeof ra)) {
        fail("decode", m, n);
        continue;
      }
      if ((dra != COBS_RET_SUCCESS) || (da != n) || (n && pcmp(ra, dec, n))) {
        fail("roundtrip", m, n);
        continue;
      }
      ++passed;
    }
  }

  /* A long zero run and a long data run, the two SWAR fast lanes. */
  for (unsigned n = 1; n <= PAYLOAD_MAX; ++n) {
    for (unsigned fillv = 0; fillv < 2u; ++fillv) {
      pset(dec, fillv ? 0x41 : 0x00, n);
      size_t la = 0, lb = 0;
      pset(a, 0xCD, sizeof a);
      pset(b, 0xCD, sizeof b);
      if ((cobs_ref_encode(dec, n, a, sizeof a, &la) !=
           cobs_encode(dec, n, b, sizeof b, &lb)) ||
          (la != lb) || pcmp(a, b, sizeof a)) {
        fail(fillv ? "run-data" : "run-zero", 0, n);
      } else {
        ++passed;
      }
    }
  }

  probe_puts(failed ? "RESULT FAIL " : "RESULT PASS ");
  probe_putu(passed);
  probe_puts("/");
  probe_putu(passed + failed);
  probe_puts(" word=");
  probe_putu((unsigned long)(sizeof(size_t) * 8u));
  probe_puts("\r\n");
  return failed != 0;
}

int main(void) {
  probe_init();
  int const bad = run();
  probe_done(bad);
  return bad;
}
