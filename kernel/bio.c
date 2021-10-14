// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#define NBUCKET 13
struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  
} bcache;
struct {
    struct spinlock lock;
    struct buf head;
}bucket[NBUCKET];

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  for(int i=0;i<NBUCKET;i++){
    //初始化桶内的头节点为双端链表
    bucket[i].head.next=&bucket[i].head;
    bucket[i].head.prev=&bucket[i].head;
    //初始化每个桶的锁
    initlock(&bucket[i].lock,"bucket");
    //按取余的方式将buffer平均分配在每个桶中
    //采用头插法
    for(b = bcache.buf+i; b < bcache.buf+NBUF; b+=NBUCKET){
      initsleeplock(&b->lock, "buffer");
      b->next=bucket[i].head.next;
      b->prev = &bucket[i].head;
      bucket[i].head.next = b;
      bucket[i].head.next->prev = b;
    }
  }
  
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int needIndex = blockno%NBUCKET;
  acquire(&bucket[needIndex].lock);

  // Is the block already cached?
  //先在自己的桶内查找，是否有dev和blockno对应的桶
  for(b = bucket[needIndex].head.next; b != &bucket[needIndex].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bucket[needIndex].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  //查找未果，进入淘汰阶段，查找lru空闲buffer进行替换
  //为保证查找和淘汰的原子性以及防止死锁，先释放needIndex的桶锁，然后获取大锁bcache lock
  //再获取needIndex的锁，防止其他进程再释放锁之后更新了桶内的buffer，需要对桶内进行第二次查找
  release(&bucket[needIndex].lock);
  acquire(&bcache.lock);
  acquire(&bucket[needIndex].lock);
  for(b = bucket[needIndex].head.next; b != &bucket[needIndex].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bucket[needIndex].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  //若在needIndex二次查找未果，进入淘汰
  //先在needIndex的桶内寻找空闲的lruBuffer
  struct buf* lruBuf=0;
  for(b = bucket[needIndex].head.next; b != &bucket[needIndex].head; b = b->next){
    //找到桶内时间戳最小的空闲buffer
    if(b->refcnt==0&&(lruBuf==0||lruBuf->timestamp>b->timestamp))lruBuf = b;
    //时间戳为0说明该buffer初始化后还未使用，必为lru空闲buffer
    if(lruBuf!=0&&lruBuf->timestamp==0)break;
  }
  //若在needIndex桶中找到lruBuffer，重置相关参数
  if(lruBuf){
    lruBuf->dev = dev;
    lruBuf->blockno = blockno;
    lruBuf->valid = 0;
    lruBuf->refcnt = 1;
    release(&bucket[needIndex].lock);
    release(&bcache.lock);
    acquiresleep(&lruBuf->lock);
    return lruBuf;
  }

  //若needIndex桶内没有找到lru空闲buffer需要去别的桶中偷
  for(int i=0;i<NBUCKET;i++){
    //碰到needIndex 跳过
    if(i==needIndex)continue;
    acquire(&bucket[i].lock);
    lruBuf=0;
    for(b = bucket[i].head.next; b != &bucket[i].head; b = b->next){
      //找到桶内时间戳最小的空闲buffer
      if(b->refcnt==0&&(lruBuf==0||lruBuf->timestamp>b->timestamp))lruBuf = b;
      //时间戳为0说明该buffer初始化后还未使用，必为lru空闲buffer
      if(lruBuf&&lruBuf->timestamp==0)break;
    }

    //若i桶内存在空闲lruBuf，先把它从i桶中取出来，然后插入needIndex桶中（头插），最后更新参数
    if(lruBuf){
      //取出lrubuf
      lruBuf->prev->next = lruBuf->next;
      lruBuf->next->prev = lruBuf->prev;
      release(&bucket[i].lock);

      //插入needIndex桶内，头插法
      lruBuf->next = bucket[needIndex].head.next;
      lruBuf->prev = &bucket[needIndex].head;
      bucket[needIndex].head.next->prev = lruBuf;
      bucket[needIndex].head.next = lruBuf;

      //重置参数
      lruBuf->dev = dev;
      lruBuf->blockno = blockno;
      lruBuf->valid = 0;
      lruBuf->refcnt = 1;
      release(&bucket[needIndex].lock);
      release(&bcache.lock);
      acquiresleep(&lruBuf->lock);
      return lruBuf;
    }

    //i桶中查找空闲lrubuf未果，释放i桶的锁，查找下一个桶
    release(&bucket[i].lock);


  }
  //所有的buffer都在被进程使用，找不到lru空闲buf，返回panic
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  int index = b->blockno%NBUCKET;
  acquire(&bucket[index].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    //buffer引用计数为0，进入空闲状态，重置时间戳
    b->timestamp = ticks;
  }
  
  release(&bucket[index].lock);
}

void
bpin(struct buf *b) {
  int index = b->blockno%NBUCKET;
  acquire(&bucket[index].lock);
  b->refcnt++;
  release(&bucket[index].lock);
}

void
bunpin(struct buf *b) {
  int index = b->blockno%NBUCKET;
  acquire(&bucket[index].lock);
  b->refcnt--;
  release(&bucket[index].lock);
}


