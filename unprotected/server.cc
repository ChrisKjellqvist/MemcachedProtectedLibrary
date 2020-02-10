#include <pku_memcached.h>
#include <memcached.h>

int main(){
  memcached_init();
  server_thread(nullptr);
}
