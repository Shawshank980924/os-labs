#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //
  //因为可能有多个进程同时发送数据，需要加锁
  acquire(&e1000_lock);
  //取出上一个发送完毕的描述符
  int id = regs[E1000_TDT];
  //若还没发送完毕返回错误
  if(!(tx_ring[id].status&E1000_TXD_STAT_DD)){
    release(&e1000_lock);
    return -1;
  }
  //释放描述符对应的mbuf
  if(tx_mbufs[id]){
    mbuffree(tx_mbufs[id]);
    tx_mbufs[id]=0;
  }
  //重置描述符
  memset(&tx_ring[id],0,sizeof (struct tx_desc));
  //把要发送的数据挂载到该描述符中
  tx_ring[id].addr = (uint64)m->head;
  tx_ring[id].length = m->len;
  
  //记录cmd标志位：
  //RS标记后网卡在传输完毕后会更新描述符的状态位
  //EOP表示该描述符是数据包的最后一个描述符
  //由于以太网的MTU为1500字节
  //观察mbuf的数据部分有2048个字节，故一个数据包必定只用一个描述符单位，也就是说描述符必然是最后一个
  tx_ring[id].cmd = E1000_TXD_CMD_RS|E1000_TXD_CMD_EOP;
  //记录mbuf的指针一遍下次遍历到的时候释放
  tx_mbufs[id] = m;
  //更新已发送完毕的寄存器
  regs[E1000_TDT] = (id+1)%TX_RING_SIZE;
  release(&e1000_lock);
  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //
  //由于该部分是网卡接收完并转存数据完毕向cpu发起中断部分，e1000_intr()
  //不存在并发的问题，因为不是进程调用的该函数，所以不需要加锁
  //这里需要处理发给该mac地址下主机所有端口（进程）的数据包，所以需要循环不断读取环形队列
  //可能有多个数据包
  while(1){
    //获取当前需要接收的数据帧对应的描述符索引
    int id  = (regs[E1000_RDT]+1)%RX_RING_SIZE;
    //若全部接收完毕了退出
    if(!(rx_ring[id].status&E1000_RXD_STAT_DD))return;
    //根据描述符更改数据帧的长度
    rx_mbufs[id]->len = rx_ring[id].length;
    //通过net_rx，然后调用net_rx_ip 和net_rx_arp进入网络层，然后调用net_rx_udp进入传输层，然后分发给各个进程
    net_rx(rx_mbufs[id]);
    //注意net_rx在传输完毕后会自动释放这个mbuf，所以需要为这个描述符重新分配一个mbuf
    struct mbuf* m = mbufalloc(0);
    rx_ring[id].addr = (uint64)m->head;
    rx_mbufs[id] = m;
    rx_ring[id].status =0;
    //传输完毕后改动E1000_RXD_STAT_DD，更新最后一个完成接收的数据报
    regs[E1000_RDT] = id;

  }
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
