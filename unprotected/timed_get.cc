/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include <pku_memcached.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <hodor-plib.h>
#include <hodor.h>

int main(){
  memcached_init(0);
  memcached_return_t ret;

  const char * key = "name";
  const char * dat = "chris";
  size_t data_len;
  uint32_t flags;

  ret = memcached_add_internal(key, strlen(key), dat, strlen(dat), 1, 0);
  assert(ret == MEMCACHED_SUCCESS);

  memcached_get_internal(key, strlen(key), &data_len, &flags, &ret);
  assert(ret == MEMCACHED_SUCCESS);
  sleep(1);
  memcached_get_internal(key, strlen(key), &data_len, &flags, &ret);
  sleep(1);
  memcached_get_internal(key, strlen(key), &data_len, &flags, &ret);
  sleep(1);
  memcached_get_internal(key, strlen(key), &data_len, &flags, &ret);
  sleep(1);
  memcached_get_internal(key, strlen(key), &data_len, &flags, &ret);
  sleep(1);
  memcached_get_internal(key, strlen(key), &data_len, &flags, &ret);
  assert(ret != MEMCACHED_SUCCESS);

  printf("succesfully verified things can expire\n");
  return 0;
}
