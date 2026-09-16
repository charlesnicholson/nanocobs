/* Probe I/O for qemu-system-arm -M lm3s6965evb: vector table, startup, UART0. */

#include <stddef.h>
#include <stdint.h>

// gcc rewrites byte loops into these, so a freestanding link needs them defined.
// The attribute stops it doing that to their own bodies and recursing.
__attribute__((optimize("no-tree-loop-distribute-patterns"))) void *memset(void *d,
                                                                          int c,
                                                                          size_t n) {
  unsigned char *p = (unsigned char *)d;
  while (n--) {
    *p++ = (unsigned char)c;
  }
  return d;
}

__attribute__((optimize("no-tree-loop-distribute-patterns"))) void *memcpy(void *d,
                                                                          void const *s,
                                                                          size_t n) {
  unsigned char *p = (unsigned char *)d;
  unsigned char const *q = (unsigned char const *)s;
  while (n--) {
    *p++ = *q++;
  }
  return d;
}

extern uint32_t _data_load, _data_start, _data_end, _bss_start, _bss_end, _stack_top;

int main(void);
void reset_handler(void);

__attribute__((section(".vectors"), used)) static void *const vectors[] = {
  &_stack_top, (void *)reset_handler
};

void reset_handler(void) {
  uint32_t const *s = &_data_load;
  for (uint32_t *d = &_data_start; d < &_data_end;) {
    *d++ = *s++;
  }
  for (uint32_t *b = &_bss_start; b < &_bss_end;) {
    *b++ = 0;
  }
  (void)main();
  for (;;) {
  }
}

#define UART0_DR (*(volatile uint32_t *)0x4000C000u)
#define UART0_FR (*(volatile uint32_t *)0x4000C018u)

void probe_init(void) {}

static void put(char c) {
  while (UART0_FR & (1u << 5)) {  /* TXFF */
  }
  UART0_DR = (uint32_t)(unsigned char)c;
}

void probe_puts(char const *s) {
  while (*s) {
    put(*s++);
  }
}

void probe_putu(unsigned long v) {
  char b[12];
  int i = 0;
  do {
    b[i++] = (char)('0' + (v % 10uL));
    v /= 10uL;
  } while (v);
  while (i) {
    put(b[--i]);
  }
}

void probe_done(int failed) {
  (void)failed;
  probe_puts("DONE\r\n");
  /* Semihosting SYS_EXIT, so the run ends instead of burning the timeout. */
  register uint32_t r0 __asm__("r0") = 0x18u;         /* SYS_EXIT */
  register uint32_t r1 __asm__("r1") = 0x20026u;      /* ADP_Stopped_ApplicationExit */
  __asm__ volatile("bkpt #0xAB" : : "r"(r0), "r"(r1) : "memory");
  for (;;) {
  }
}
