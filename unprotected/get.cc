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

#define BUFF_LEN 32
int main(){
  hodor_init();
  hodor_enter();
  std::string name = "chris";

  char nbuff[BUFF_LEN];
  size_t len;
  uint32_t flags;
  memcached_return_t err;

  strcpy(nbuff, name.c_str());
  char *str = memcached_get_internal(nbuff, strlen(nbuff), &len, &flags, &err);
  assert(err == MEMCACHED_SUCCESS);
  printf("chris");
  for(unsigned i = 0; i < len; ++i)
    printf("%c", *(str++));
  free(str);
  printf("\n");
  exit(0);
}
