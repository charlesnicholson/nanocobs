/* Probe I/O for targets with newlib semihosting, and for a native build. */

#include <stdio.h>
#include <stdlib.h>

#if defined(__arm__)
// newlib's rdimon needs this before any stdio reaches the semihosting host.
extern void initialise_monitor_handles(void);
#endif

void probe_init(void) {
#if defined(__arm__)
  initialise_monitor_handles();
#endif
  setvbuf(stdout, NULL, _IONBF, 0);
}

void probe_puts(char const *s) { fputs(s, stdout); }

void probe_putu(unsigned long v) { printf("%lu", v); }

void probe_done(int failed) {
  fputs("DONE\r\n", stdout);
  fflush(stdout);
  exit(failed);
}
