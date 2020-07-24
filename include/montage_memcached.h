#pragma once
#include "pku_memcached.h"
#include "memcached.h"

using namespace pds;

#ifdef __cplusplus
extern "C" {
#endif
  
  char* montage_get(const char *key, size_t key_length, size_t *value_length, 
          uint32_t *flags,memcached_return_t *error);
  memcached_return_t montage_put(const char * key, size_t nkey, const char * data, 
          size_t datan, uint32_t exptime, uint32_t flags);
  memcached_return_t montage_insert(const char* key, size_t nkey, const char * data, 
          size_t datan, uint32_t exptime, uint32_t flags);
  memcached_return_t montage_remove(const char* key, size_t nkey, uint32_t exptime);

#ifdef __cplusplus
}
#endif
