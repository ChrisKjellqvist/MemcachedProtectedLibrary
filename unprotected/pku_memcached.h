#ifndef PKU_MEMCACHED_H
#define PKU_MEMCACHED_H
#include <inttypes.h>
#include <pthread.h>

struct get_struct {
  char *k, *buffer; size_t nk, buffLen; uint32_t exptime;
};
struct touch_struct {
  char* k; size_t nk; uint32_t exptime;
};
struct insert_struct {
  char *k, *d; size_t nk, dn; uint32_t exptime;
};
struct start_struct {
  pthread_t *thread;
  int argc;
  char ** argv;
};

void memcached_touch(char* key, size_t nkey, uint32_t exptime, int t_id);
void memcached_insert(char* key, size_t nkey, char *data, size_t datan, uint32_t exptime, int t_id);
void  memcached_get(char *key, size_t nkey, uint32_t exptime, char *buffer, size_t buffLen, int *exit_code, int t_id);
int start_server_thread(pthread_t *thread, int argc, char** argv);
void memcached_end(int t_id);
#endif
