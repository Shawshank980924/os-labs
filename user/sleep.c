#include "kernel/syscall.h"
#include "user/user.h"


int
main(int argc, char *argv[])
{

  if(argc != 2){
    printf("please give 1 argument in command line!\n");
    exit(1);
  }
  printf("(nothing happens for a little while)\n");
  sleep(atoi(*argv+1));
  exit(0);
}
