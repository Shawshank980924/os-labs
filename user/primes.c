#include "kernel/types.h"
#include "user/user.h"

void primes(int pl_read)
{
  char prime[4];
  //在子进程中当管道中没有可读取的数据时递归结束
  if (read(pl_read, prime, 4) == 0)
  {
    close(pl_read);
    return;
  }
  //有可以读取的数据时，第一个数据必定是质数,读取并且打印
  printf("prime %d\n", *((int *)prime));
  int p[2];
  pipe(p);
  //fork进入子进程
  if (fork() == 0)
  {
    //fork会产生多余的文件描述符，需要关闭
    close(pl_read);
    close(p[1]);
    //递归
    primes(p[0]);
    exit(0);
  }
  int prm = *((int *)prime);
  char tmp[4];
  //父进程从上次递归管道中写入的数中读取并且筛选剔除是首个质数倍数的数,写入这个递归的管道
  while(read(pl_read,tmp,4)!=0){
    if( *((int *)tmp)%prm!=0){
      write(p[1],tmp,4);
    }
  }
  close(pl_read);
  close(p[1]);
  close(p[0]);
  wait(0);
  exit(0);
}

int main()
{
  int p[2];
  pipe(p);
  for (int i = 2; i < 36; i++)
  {
    write(p[1], (char *)&i, 4);
  }
  //close the pipe's write end file descriptor  
  close(p[1]);
  primes(p[0]);
  exit(0);
}