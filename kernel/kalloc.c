// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

void freerange(void *pa_start, void *pa_end,int i);
void* actual_kalloc(int idx);
void actual_kfree(void* pa,int idx);
//extern int cpuid();
extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run
{
  struct run *next;
};


struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  uint64 avg;
  uint64 p;
  int i = 0;
  for (int i = 0; i < NCPU; i++) {
    initlock(&kmem[i].lock, "kmem");
  }
  // evenly divide the free pages
  avg = (PHYSTOP - (uint64)end) / NCPU;
  for (p = (uint64)end; p <= PHYSTOP; p += avg) {
    freerange((void*)p,p+avg < PHYSTOP ? (void*)(p+avg):(void*)PHYSTOP, i);
    i++;
  }
}

void
freerange(void *pa_start, void *pa_end, int i)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    actual_kfree(p,i);
}
void
kfree(void *pa)
{ 
  int id;
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  push_off();
  id = cpuid();
  pop_off();
  actual_kfree(pa,id);
}

void actual_kfree(void* pa,int idx) {
  struct run *r;
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem[idx].lock);
  r->next = kmem[idx].freelist;
  kmem[idx].freelist = r;
  release(&kmem[idx].lock);
}

void *
kalloc(void)
{
  void* pa;
  int id;
  push_off();
  id = cpuid();
  pop_off();
  pa = actual_kalloc(id);
  if (pa) return pa;
  for (int i = 0; i < NCPU; i++) {
    pa = actual_kalloc(i);
    if (pa) return pa;
  }
  return 0;
}

void* actual_kalloc(int idx) {
  struct run *r;
  acquire(&kmem[idx].lock);
  r = kmem[idx].freelist;
  if(r)
    kmem[idx].freelist = r->next;
  release(&kmem[idx].lock);
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}