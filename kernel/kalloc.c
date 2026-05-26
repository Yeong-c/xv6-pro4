// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "proc.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// edit in pa4
struct page pages[PHYSTOP/PGSIZE];
struct page *page_lru_head;
int num_lru_pages;
struct spinlock swap_lock;

// edit in pa4
#define SWAP_BLKS_PER_PG (PGSIZE/BSIZE)
#define MAX_SWAP_SLOTS   (SWAPMAX / SWAP_BLKS_PER_PG)
uchar swap_bitmap[PGSIZE];

static int  alloc_swap_slot(void);
static void free_swap_slot_locked(int slot);
static int  swap_out(void);

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  // edit in pa4
  initlock(&swap_lock, "swap");
  page_lru_head = 0;
  num_lru_pages = 0;
  memset(swap_bitmap, 0, sizeof(swap_bitmap));
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  // edit in pa4
  for(;;){
    acquire(&kmem.lock);
    r = kmem.freelist;
    if(r)
      kmem.freelist = r->next;
    release(&kmem.lock);

    if(r){
      memset((char*)r, 5, PGSIZE); // fill with junk
      return (void*)r;
    }

    if(swap_out() == 0){
      printf("kalloc: out of memory\n");
      return 0;
    }
  }
}

// edit in pa4
static int
alloc_swap_slot(void)
{
  int i;
  for(i = 0; i < MAX_SWAP_SLOTS; i++){
    if((swap_bitmap[i/8] & (1 << (i%8))) == 0){
      swap_bitmap[i/8] |= (1 << (i%8));
      return i;
    }
  }
  return -1;
}

// edit in pa4
static void
free_swap_slot_locked(int slot)
{
  if(slot < 0 || slot >= MAX_SWAP_SLOTS)
    panic("free_swap_slot: bad slot");
  swap_bitmap[slot/8] &= ~(1 << (slot%8));
}

// edit in pa4
void
swap_free_slot(int slot)
{
  acquire(&swap_lock);
  free_swap_slot_locked(slot);
  release(&swap_lock);
}

// edit in pa4
void
lru_add(uint64 pa, pagetable_t pt, uint64 va)
{
  struct page *p;

  if(pa == 0 || pa >= PHYSTOP)
    panic("lru_add: bad pa");
  p = &pages[pa / PGSIZE];

  acquire(&swap_lock);
  if(p->next != 0 || p->prev != 0 || page_lru_head == p){
    release(&swap_lock);
    return;
  }
  p->pagetable = pt;
  p->vaddr = (char*)va;

  if(page_lru_head == 0){
    p->next = p;
    p->prev = p;
    page_lru_head = p;
  } else {
    p->next = page_lru_head;
    p->prev = page_lru_head->prev;
    page_lru_head->prev->next = p;
    page_lru_head->prev = p;
  }
  num_lru_pages++;
  release(&swap_lock);
}

// edit in pa4
void
lru_remove(uint64 pa)
{
  struct page *p;

  if(pa == 0 || pa >= PHYSTOP)
    return;
  p = &pages[pa / PGSIZE];

  acquire(&swap_lock);
  if(p->next == 0 && p->prev == 0 && page_lru_head != p){
    release(&swap_lock);
    return;
  }
  if(p->next == p){
    page_lru_head = 0;
  } else {
    p->prev->next = p->next;
    p->next->prev = p->prev;
    if(page_lru_head == p)
      page_lru_head = p->next;
  }
  p->next = 0;
  p->prev = 0;
  p->pagetable = 0;
  p->vaddr = 0;
  num_lru_pages--;
  release(&swap_lock);
}

// edit in pa4
static int
swap_out(void)
{
  struct page *victim;
  pte_t *pte;
  pagetable_t victim_pt;
  uint64 victim_va, victim_pa, flags;
  int slot, max_iter;
  struct proc *p;
  pagetable_t saved_pt;

  acquire(&swap_lock);

  if(page_lru_head == 0){
    release(&swap_lock);
    return 0;
  }

  victim = 0;
  max_iter = num_lru_pages * 2 + 16;
  while(max_iter-- > 0 && page_lru_head){
    struct page *cur = page_lru_head;
    pte = walk(cur->pagetable, (uint64)cur->vaddr, 0);
    if(pte == 0 || (*pte & PTE_V) == 0){
      if(cur->next == cur){
        page_lru_head = 0;
      } else {
        cur->prev->next = cur->next;
        cur->next->prev = cur->prev;
        page_lru_head = cur->next;
      }
      cur->next = cur->prev = 0;
      cur->pagetable = 0;
      cur->vaddr = 0;
      num_lru_pages--;
      continue;
    }
    if(*pte & PTE_A){
      *pte &= ~PTE_A;
      sfence_vma();
      page_lru_head = cur->next;
    } else {
      victim = cur;
      if(victim->next == victim){
        page_lru_head = 0;
      } else {
        victim->prev->next = victim->next;
        victim->next->prev = victim->prev;
        page_lru_head = victim->next;
      }
      num_lru_pages--;
      break;
    }
  }

  if(victim == 0){
    release(&swap_lock);
    return 0;
  }

  slot = alloc_swap_slot();
  if(slot < 0){
    if(page_lru_head == 0){
      victim->next = victim;
      victim->prev = victim;
      page_lru_head = victim;
    } else {
      victim->next = page_lru_head;
      victim->prev = page_lru_head->prev;
      page_lru_head->prev->next = victim;
      page_lru_head->prev = victim;
    }
    num_lru_pages++;
    release(&swap_lock);
    return 0;
  }

  victim_pa = (uint64)(victim - pages) * PGSIZE;
  victim_pt = victim->pagetable;
  victim_va = (uint64)victim->vaddr;

  victim->next = 0;
  victim->prev = 0;
  victim->pagetable = 0;
  victim->vaddr = 0;

  release(&swap_lock);

  p = myproc();
  saved_pt = p->pagetable;
  p->pagetable = victim_pt;
  swapwrite(victim_va, slot);
  p->pagetable = saved_pt;

  pte = walk(victim_pt, victim_va, 0);
  if(pte == 0)
    panic("swap_out: pte vanished");
  flags = PTE_FLAGS(*pte);
  *pte = ((uint64)slot << 10) | (flags & ~PTE_V);
  sfence_vma();

  kfree((void*)victim_pa);
  return 1;
}

// edit in pa4
int
swap_in(pagetable_t pt, uint64 va)
{
  pte_t *pte;
  uint64 new_pa, flags;
  int slot;
  struct proc *p;
  pagetable_t saved_pt;

  va = PGROUNDDOWN(va);
  pte = walk(pt, va, 0);
  if(pte == 0)
    return -1;
  if(*pte & PTE_V)
    return 0;
  if(*pte == 0 || (*pte & PTE_U) == 0)
    return -1;

  slot  = (int)(*pte >> 10);
  flags = PTE_FLAGS(*pte);

  new_pa = (uint64)kalloc();
  if(new_pa == 0)
    return -1;

  *pte = PA2PTE(new_pa) | flags | PTE_V | PTE_W;
  sfence_vma();

  p = myproc();
  saved_pt = p->pagetable;
  p->pagetable = pt;
  swapread(va, slot);
  p->pagetable = saved_pt;

  *pte = PA2PTE(new_pa) | flags | PTE_V;
  sfence_vma();

  swap_free_slot(slot);
  lru_add(new_pa, pt, va);
  return 0;
}
