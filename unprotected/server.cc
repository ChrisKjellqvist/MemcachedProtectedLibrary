#include <pku_memcached.h>
#include <memcached.h>

extern bool server_flag;
int main(){
  server_flag = true;
#ifdef PMDK
  pop = pmemobj_create(HEAP_FILE, "test", REGION_SIZE, 0666);
  if (pop == nullptr) {
    perror("pmemobj_create");
    return 1;
  }
#endif
  memcached_init();
  memcached_start_server();
}
