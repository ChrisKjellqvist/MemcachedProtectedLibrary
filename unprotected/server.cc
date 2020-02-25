#include <pku_memcached.h>
#include <memcached.h>
#include <hodor.h>

int main(){
  int ret = hodor_init();
  assert(ret == 0);
  ret = hodor_enter();
  assert(ret == 0);
  printf("success2\n");
  fflush(stdout);
  
  unsigned int eax, edx;
  unsigned int ecx = 0;
  unsigned int pkru;

  asm volatile(".byte 0x0f,0x01,0xee\n\t" : "=a"(eax), "=d"(edx) : "c"(ecx));
  pkru = eax;
  printf("unprotected: %x\n", pkru);
  fflush(stdout);
  memcached_start_server();
}
