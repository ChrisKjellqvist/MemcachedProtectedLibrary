#include <rpmalloc.hpp>
#include <BaseMeta.hpp>
#include <assert.h>

int main(){
  RP_init("memcached.rpma");
  unsigned *al = RP_get_root<unsigned>(0);
//  RP_recover();
  for(int i = 0; i < 1000; ++i){
    assert(al[i] == 0xDEADBEEF);
  }
}
