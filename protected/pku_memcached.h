#ifndef PKU_MEMCACHED_H
#define PKU_MEMCACHED_H
#include <inttypes.h>
#include <pthread.h>

extern "C" {
void memcached_touch  (char* key, size_t nkey, uint32_t exptime,                                int t_id);
void memcached_insert (char* key, size_t nkey, uint32_t exptime, char *data,    size_t datan,   int t_id);
int  memcached_get    (char* key, size_t nkey, uint32_t exptime, char *buffer,  size_t buffLen, int t_id);
void memcached_end    (int t_id);

void memcached_init();
}

int start_server_thread(pthread_t *thread, int argc, char** argv);

// include/hodor_plib.h
// HODOR_FUNC_ATTR
// HODOR_INIT_FUNC
// LOCALDISK, qemu-clea.img
#endif
