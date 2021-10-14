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

void freerange(void *pa_start, void *pa_end);
// void* actual_kalloc(int idx);
// void actual_kfree(void* pa,int idx);
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
    freerange((void*)p,p+avg < PHYSTOP ? (void*)(p+avg):(void*)PHYSTOP);
    i++;
  }
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
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
  struct run *r;
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
  
}

void *
kalloc(void)
{
  int id;
  push_off();
  id = cpuid();
  pop_off();
  struct run *r;
  //先在自己的freelist里面找
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);//注意提前释放锁防止死锁
  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    return (void*)r;
  }
    

  //自己freelist里面没有空白页就在其他cpu的空白页里面找
  for (int i = 0; i < NCPU; i++) {
    acquire(&kmem[i].lock);
    r = kmem[i].freelist;
    if(r)
      kmem[i].freelist = r->next;
    release(&kmem[i].lock);
    if(r){
      memset((char*)r, 5, PGSIZE); // fill with junk
      return (void*)r;
    }
      
  }
  //都没找到返回零
  return 0;
}

