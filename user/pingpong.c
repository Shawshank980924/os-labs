#include "kernel/types.h"
#include "user/user.h"
//use two pipes to send byte in two different directions(father->child;child->father)



int
main(int argc, char *argv[])
{
  int p1[2],p2[2];
  //p1 for father->child;p2 for child -> father
  char buf[]={"A"};
  // one byte to send, so use char type

 pipe(p1);
 pipe(p2);
 //make p1 and p2 a pair of pair
  int pid = fork();
  if(pid == 0){
    //child process
    close(p1[1]);
    close(p2[0]);
    //close the unused end of pipe 
    if(read(p1[0],buf,1)!=1){
      printf("father->child read error!\n");
      exit(1);
    }
    if(write(p2[1],buf,1)!=1){
      printf("child -> father write error!\n");
      exit(1);
    }
    //first read then write
    printf("%d: received ping\n",getpid());
  }
  else{
    // father process
    close(p1[0]);
    close(p2[1]);
    if(write(p1[1],buf,1)!=1){
      printf("father -> child write error!\n");
      exit(1);
    }
    if(read(p2[0],buf,1)!=1){
      printf("child -> father read error!\n");
      exit(1);
    }
    //first write then read
    wait(0);
    //wait until child process end
    printf("%d: received pong\n",getpid());
  }
  exit(0);
}
