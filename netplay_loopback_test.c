#include "netplay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
void netplay_test_drop(int);
static void quiet(const char*m){(void)m;}
static int run(int host,int port,int frames,int seed,int drop,int jitter,const char*out,int corrupt_at){
  srand(seed); netplay_test_drop(drop);
  if(netplay_start(host,"127.0.0.1",port,3,quiet))return 1;
  FILE*f=fopen(out,"w");
  for(int i=0;i<frames;i++){
    uint8_t loc[16],r[16],l[16];
    for(int k=0;k<16;k++)loc[k]=(uint8_t)(rand()&0x3);   /* scripted local input */
    if(jitter)usleep((rand()%jitter)*1000);              /* uneven speed */
    uint32_t chk=1000+i; if(i==corrupt_at)chk^=1;
    if(!netplay_frame(chk,loc,r,l)){fprintf(f,"DROPPED %d\n",i);break;}
    for(int k=0;k<16;k++)fprintf(f,"%02x",r[k]);fprintf(f," ");
    for(int k=0;k<16;k++)fprintf(f,"%02x",l[k]);fprintf(f,"\n");
  }
  fprintf(f,"desync=%d\n",netplay_desynced());fclose(f);netplay_stop();return 0;}
int main(int c,char**v){
  int drop=atoi(v[1]),jit=atoi(v[2]),corrupt=c>3?atoi(v[3]):-1,port=20000+getpid()%20000,frames=900;
  pid_t p=fork();
  if(!p){usleep(150000);return run(0,port,frames,2,drop,jit,"/tmp/np/client.txt",corrupt);}
  int rc=run(1,port,frames,1,drop,jit,"/tmp/np/host.txt",-1);
  int st;waitpid(p,&st,0);return rc;}
