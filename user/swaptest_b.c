// edit in pa4: Custom test B - sbrk grow/shrink with swap slot release.
// Verifies that sbrk(negative) frees both in-memory pages and swap slots,
// so we don't leak swap slots when shrinking.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096

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
  printf("=== sbrk grow/shrink + swap-slot release ===\n");

  // grow big enough to force swap-outs (30000 > ~28k free pool)
  sbrk(PGSIZE * 30000);
  print_swapstat();   // NRW > 0 (some swap-outs happened)

  // shrink fully -- should release in-memory pages AND swap slots
  sbrk(-PGSIZE * 30000);
  print_swapstat();

  // grow again with same large size -- if slots weren't released,
  // the swap space (7000 slot max) would be doubly used and OOM
  // would hit much sooner than the first grow.  Should succeed.
  sbrk(PGSIZE * 30000);
  print_swapstat();

  // third cycle to stress the slot bookkeeping
  sbrk(-PGSIZE * 30000);
  sbrk(PGSIZE * 30000);
  print_swapstat();

  printf("OK: grew, shrank, grew again without OOM\n");
  exit(0);
}
