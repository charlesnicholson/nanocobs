/* Probe I/O for qemu-system-avr: USART0, then spin, since the guest cannot halt it. */

#include <avr/io.h>

static void put(char c) {
  while (!(UCSR0A & (1 << UDRE0))) {
  }
  UDR0 = (unsigned char)c;
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
  for (;;) {
  }
}
