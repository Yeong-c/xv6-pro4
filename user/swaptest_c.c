// edit in pa4: Custom test C - fork copies swapped pages correctly.
// Parent writes a marker into each page, swap-outs happen, then forks.
// Child must see the parent's data (swap-in of swapped parent pages).
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define NPAGES 15000  // 2*N > free pool but < free pool + swap slots

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
  printf("=== fork copies swapped pages ===\n");

  char *base = (char *)sbrk(PGSIZE * NPAGES);
  if (base == (char *)-1) {
    printf("sbrk failed\n");
    exit(1);
  }

  // Write a unique marker to each page so we can verify after fork.
  for (int i = 0; i < NPAGES; i++)
    base[i * PGSIZE] = (char)((i * 7 + 3) & 0xff);

  print_swapstat();   // post-write

  int pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    // Child: verify every page got copied correctly.
    int errors = 0;
    for (int i = 0; i < NPAGES; i++) {
      char expected = (char)((i * 7 + 3) & 0xff);
      char got = base[i * PGSIZE];
      if (got != expected) {
        if (errors < 5)
          printf("CHILD MISMATCH page %d: exp %d got %d\n",
                 i, (int)expected, (int)got);
        errors++;
      }
    }
    if (errors == 0)
      printf("CHILD OK: %d pages match\n", NPAGES);
    else
      printf("CHILD FAIL: %d mismatches\n", errors);
    print_swapstat();
    exit(0);
  }

  wait(0);
  print_swapstat();
  printf("PARENT done\n");
  exit(0);
}
