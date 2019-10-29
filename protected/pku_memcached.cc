#include <pku_memcached.h>
#include <memcached.h>

#include <stdlib.h>
#include <hodor-plib.h>
#include <hodor.h>
#include <sys/mman.h>
#include <rpmalloc.hpp>

extern pthread_mutex_t end_mutex;

extern "C" {

HODOR_FUNC_ATTR void memcached_touch(char* key, size_t nkey, uint32_t exptime,
    int t_id){
  pku_memcached_touch(key, nkey, exptime);
} HODOR_FUNC_EXPORT(memcached_touch, 4);

HODOR_FUNC_ATTR void memcached_insert(char* key, size_t nkey, uint32_t exptime,
    char* data, size_t datan, int t_id){ 
  pku_memcached_insert(key, nkey, data, datan, exptime);
} HODOR_FUNC_EXPORT(memcached_insert, 6);

HODOR_FUNC_ATTR int  memcached_get(char* key, size_t nkey, uint32_t exptime,
    char* buffer, size_t buffLen, int t_id){
  return pku_memcached_get(key, nkey, exptime, buffer, buffLen);
} HODOR_FUNC_EXPORT(memcached_get, 6);

HODOR_FUNC_ATTR void memcached_end(int t_id){
  pthread_mutex_unlock(&end_mutex);
} HODOR_FUNC_EXPORT(memcached_end, 1);

// Start memcached maintainence processes
void memcached_init(){
  is_restart = RP_init("memcached.rpma");
  // TODO: make sure private stack exists... We used to do this.
  void *start, *end;
  int i = 0;
  while (!RP_region_range(i++, &start, &end)){
    ptrdiff_t rp_region_len = (char*)start - (char*)end; 
    pkey_mprotect(start, rp_region_len, PROT_READ | PROT_WRITE | PROT_EXEC, 1);
  }
} HODOR_INIT_FUNC(memcached_init);
}

