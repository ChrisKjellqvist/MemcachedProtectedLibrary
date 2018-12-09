#include "plib.h"
#include "memcached.h"
#include <stdio.h>
#include <stdlib.h>
extern char __linker_plib_addr__;
extern char __linker_plib_len__;
int pkey;
extern int not_main();
int main(int argc, char** argv){
#ifndef NO_PKEY
  printf("using pkeys\n");
  pkey = pkey_alloc(0, PROT_READ | PROT_WRITE | PROT_EXEC);
  pkey_mprotect(&__linker_plib_addr__, (unsigned long)&__linker_plib_len__, PROT_READ | PROT_WRITE | PROT_EXEC, pkey);
  pkey_set(1, PKEY_DISABLE_ACCESS);
#endif
  pthread_t my_server;
  start_server_thread(&my_server, argc, argv);
  not_main(argc, argv);
  exit(0); 
}

