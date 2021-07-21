#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int
main(int argc, char *argv[])
{
  char buf[32],temp[32];
  char* p = buf;
  int k;
  char* arg_total[32];
  int i,j,m=0;
  for(i=0;i<argc-1;i++){
    arg_total[i]=argv[i+1];
  }
  while((k=read(0,temp,sizeof(temp)))!=0){
    for(j=0;j<k;j++){
      if(temp[j]=='\n'){
        arg_total[i++]=p;
        buf[m]=0;
        arg_total[i]=0;
        i = argc-1;
        p = buf;
        m=0;
        if(fork()==0){
          exec(argv[1],arg_total);
        }
        wait(0);
      }
      else if(temp[j]==' '){
        arg_total[i++]=p;
        buf[m++]=0;
        p=&buf[m];
      }
      else{
        buf[m++]=temp[j];
      }
    }
  }

exit(0);
}
