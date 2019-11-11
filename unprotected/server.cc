#include <pku_memcached.h>
#include <memcached.h>

int main(){
  memcached_init(1);
  server_thread(nullptr);
}
