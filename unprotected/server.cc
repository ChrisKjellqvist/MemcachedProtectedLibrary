#include <pku_memcached.h>
#include <memcached.h>
#include <hodor.h>

int main(){
  int ret = hodor_init();
  assert(ret == 0);
  ret = hodor_enter();
  assert(ret == 0);
  memcached_init();
  memcached_start_server();
}
