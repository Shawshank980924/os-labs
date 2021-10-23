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
void kfree_on_cpuid(void* pa,int id);
void freerange(void *pa_start, void *pa_end,int i);
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
  // avg是每个cpu分配的空白物理页地址范围大小
  avg = (PHYSTOP - (uint64)end) / NCPU;
  for (p = (uint64)end; p <= PHYSTOP; p += avg) {
    //给cpuid为i的cpu所属freelist分配空白物理页
    freerange((void*)p,p+avg < PHYSTOP ? (void*)(p+avg):(void*)PHYSTOP,i);
    i++;
  }
}

void
freerange(void *pa_start, void *pa_end,int id)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree_on_cpuid(p,id);
}
void* kalloc_on_cpuid(int id){
  struct run *r;
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);
  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  return (void*)r;
}
void kfree_on_cpuid(void* pa,int id){
  struct run *r;
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  //插入空白页，并更新freelist头指针
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
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
  kfree_on_cpuid(pa,id);
  
}

void *
kalloc(void)
{
  int id;
  void* pa;
  push_off();
  id = cpuid();
  pop_off();
  //先在自己的freelist里面找有无空白页
  pa = kalloc_on_cpuid(id);
  if(pa)
    return pa;

  //自己freelist里面没有空白页就在其他cpu的freelist里面去偷
  for (int i = 0; i < NCPU; i++) {
    if(i==id)continue;
    pa = kalloc_on_cpuid(i);
    if(pa)
      return pa;
      
  }
  //都没找到返回零
  return 0;
}

