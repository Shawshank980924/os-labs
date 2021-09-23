// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.
extern int refNum[];
struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int refNum[PGROUNDUP(PHYSTOP) /PGSIZE];
} refct;
void rcinit(){
  initlock(&refct.lock,"refct");
  for(int i=0;i<PGROUNDUP(PHYSTOP) /PGSIZE;i++){
    acquire(&refct.lock);
    refct.refNum[i]=0;
    release(&refct.lock);
  }
}
void inc_refNum(uint64 pa){
  acquire(&refct.lock);
  refct.refNum[pa/PGSIZE]++;
  release(&refct.lock);
}
void dec_refNum(uint64 pa){
  acquire(&refct.lock);
  refct.refNum[pa/PGSIZE]--;
  release(&refct.lock);
}
int get_refNum(uint64 pa){
  int n ;
  acquire(&refct.lock);
  n = refct.refNum[pa/PGSIZE];
  release(&refct.lock);
  return n;
}
void
kinit()
{
  rcinit();
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}


void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    inc_refNum((uint64)p);
    kfree(p);
  }
    
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  acquire(&kmem.lock);
  dec_refNum((uint64)pa);
  if(get_refNum((uint64)pa)!=0){
    release(&kmem.lock);
    return ;//only free the physical page when refNum==0
  } 
  // Fill with junk to catch dangling refs.
  release(&kmem.lock);
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

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);
  if(r)
    inc_refNum((uint64)r);
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
