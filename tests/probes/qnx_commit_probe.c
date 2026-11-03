/*
 * qnx_commit_probe.c - how much private anonymous memory QNX lets a process
 * make writable. Explains the 4 upstream unit tests that need a 4 GiB linear
 * memory and fail on a 1 GiB target: QNX does not overcommit.
 *
 * Copyright 2026 Selman Altinay
 * SPDX-License-Identifier: Apache-2.0
 *
 * Build: qcc -Vgcc_ntox86_64 qnx_commit_probe.c -o qnx_commit_probe
 */
#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <sys/resource.h>
int main(void){
  struct rlimit r; getrlimit(RLIMIT_AS,&r); printf("RLIMIT_AS cur %llx max %llx\n",(unsigned long long)r.rlim_cur,(unsigned long long)r.rlim_max);
  size_t sizes[]={1ull<<30, 4ull<<30, 8ull<<30};
  int flags[]={0, MAP_LAZY};
  for(int f=0;f<2;f++) for(int i=0;i<3;i++){
    void*p=mmap(NULL,sizes[i],PROT_NONE,MAP_ANON|MAP_PRIVATE|flags[f],-1,0);
    printf("lazy=%d reserve %zuG PROT_NONE: %s\n",f,sizes[i]>>30,p==MAP_FAILED?strerror(errno):"ok");
    if(p!=MAP_FAILED){ int rc=mprotect(p,sizes[i]/2,PROT_READ|PROT_WRITE); printf("   mprotect RW %zuG: %s\n",(sizes[i]/2)>>30, rc?strerror(errno):"ok"); munmap(p,sizes[i]); }
  }
  void*p=mmap(NULL,4ull<<30,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE|MAP_LAZY,-1,0); printf("direct RW lazy 4G: %s\n",p==MAP_FAILED?strerror(errno):"ok");
  return 0;}
