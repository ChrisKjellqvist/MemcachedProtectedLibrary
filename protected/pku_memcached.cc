#include <pku_memcached.h>
#include <memcached.h>

#include <stdlib.h>
#include <hodor-plib.h>
#include <hodor.h>
#include <sys/mman.h>
#include <rpmalloc.hpp>

extern std::atomic<int> *end_signal;

extern "C" {

HODOR_FUNC_ATTR void memcached_touch(char* key, size_t nkey, uint32_t exptime){
  pku_memcached_touch(key, nkey, exptime);
} HODOR_FUNC_EXPORT(memcached_touch, 4);

HODOR_FUNC_ATTR void memcached_insert(char* key, size_t nkey, uint32_t exptime,
    char* data, size_t datan){ 
  pku_memcached_insert(key, nkey, data, datan, exptime);
} HODOR_FUNC_EXPORT(memcached_insert, 6);

HODOR_FUNC_ATTR int  memcached_get(char* key, size_t nkey, uint32_t exptime,
    char* buffer, size_t buffLen){
  return pku_memcached_get(key, nkey, exptime, buffer, buffLen);
} HODOR_FUNC_EXPORT(memcached_get, 6);

HODOR_FUNC_ATTR void memcached_end(){
  end_signal->store(1);
} HODOR_FUNC_EXPORT(memcached_end, 1);

// Start memcached maintainence processes
// server is either 0 or 1 to represent whether or not we are initializing
// for a server process or a client process
void memcached_init(int server){
  is_restart = RP_init("memcached.rpma");
  printf("is restart? %d\n", is_restart);
  is_server = server;
  int i = 0;
  void *start, *end;
  while (!RP_region_range(i++, &start, &end)){
    ptrdiff_t rp_region_len = (char*)start - (char*)end;
    pkey_mprotect(start, rp_region_len, PROT_READ | PROT_WRITE | PROT_EXEC, 1);
  }

  agnostic_init();

} HODOR_INIT_FUNC(memcached_init);
}

