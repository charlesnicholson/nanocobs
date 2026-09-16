/* Probe I/O for targets with newlib semihosting, and for a native build. */

#include <stdio.h>
#include <stdlib.h>

void probe_puts(char const *s) { fputs(s, stdout); }

void probe_putu(unsigned long v) { printf("%lu", v); }

void probe_done(int failed) {
  fputs("DONE\r\n", stdout);
  fflush(stdout);
  exit(failed);
}
