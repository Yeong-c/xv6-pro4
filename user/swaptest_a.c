// edit in pa4: Custom test A - swap-in correctness (data integrity).
// Verifies that pages swapped out then read back retain their content.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define NPAGES 28500  // ~1 page over physical capacity -> some swap-outs

void
print_swapstat(void)
{
  int nrr, nrw;
  swapstat(&nrr, &nrw);
  printf("swapstat: NRR: %d, NRW: %d\n", nrr, nrw);
}

int
main(void)
{
  printf("=== swap-in data integrity ===\n");

  // grow heap by NPAGES pages
  char *base = (char *)sbrk(PGSIZE * NPAGES);
  if (base == (char *)-1) {
    printf("sbrk failed\n");
    exit(1);
  }
  print_swapstat();

  // write a unique byte to the first byte of every page
  for (int i = 0; i < NPAGES; i++) {
    base[i * PGSIZE] = (char)(i & 0xff);
  }
  print_swapstat(); // many writes triggered swap-out

  // read back and verify
  int errors = 0;
  for (int i = 0; i < NPAGES; i++) {
    char v = base[i * PGSIZE];
    if (v != (char)(i & 0xff)) {
      if (errors < 5)
        printf("MISMATCH at page %d: expected %d got %d\n",
               i, (int)(char)(i & 0xff), (int)v);
      errors++;
    }
  }
  print_swapstat(); // swap-ins triggered above

  if (errors == 0)
    printf("OK: all %d pages have correct data\n", NPAGES);
  else
    printf("FAIL: %d mismatches\n", errors);

  exit(0);
}
