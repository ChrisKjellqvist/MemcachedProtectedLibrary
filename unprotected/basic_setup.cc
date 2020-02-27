/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include <rpmalloc.hpp>
#include <BaseMeta.hpp>

int main(){
  RP_init("memcached.rpma");
  int *al = (int*)pm_malloc(sizeof(int)*1000);
  RP_set_root(al, 0);
  for(int i = 0; i < 1000; ++i)
    al[i] = 0xDEADBEEF;
}
