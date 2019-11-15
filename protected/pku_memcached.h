#ifndef PKU_MEMCACHED_H
#define PKU_MEMCACHED_H
#include <inttypes.h>
#include <pthread.h>

extern "C" {
void memcached_touch  (char* key, size_t nkey, uint32_t exptime);
void memcached_insert (char* key, size_t nkey, uint32_t exptime, char *data,    size_t datan);
int  memcached_get    (char* key, size_t nkey, uint32_t exptime, char *buffer,  size_t buffLen);
int memcached_set     (char* key, size_t nkey, char *data, size_t datan, 
    uint32_t exptime);
void memcached_end    (int t_id);

void memcached_init(int server);
}

// include/hodor_plib.h
// HODOR_FUNC_ATTR
// HODOR_INIT_FUNC
// LOCALDISK, qemu-clea.img
#endif
