#include "montage_memcached.h"
#include "persist_struct_api.hpp"
#include "pblk_naked.hpp"
#include "EpochSys.hpp"

using namespace pds;

char* montage_get(const char *key, size_t key_length, size_t *value_length, uint32_t *flags, memcached_return_t *error)
{
  *error = MEMCACHED_FAILURE;
  char *buff = NULL;
  *error = pku_memcached_get(key, key_length, buff, value_length, flags);
  return buff;
}
  
memcached_return_t montage_put(const char * key, size_t nkey, const char * data, size_t datan, uint32_t exptime, uint32_t flags)
{
  auto ret = pku_memcached_set(key, nkey, data, datan, exptime);
  if(ret){
    BEGIN_OP_AUTOEND(ret);
    return MEMCACHED_SUCCESS;
  }else{
    return MEMCACHED_FAILURE;
  }
}

memcached_return_t montage_insert(const char* key, size_t nkey, const char * data, size_t datan, uint32_t exptime, uint32_t flags)
{
  auto ret = pku_memcached_insert(key, nkey, data, datan, exptime);
  if(ret){
    BEGIN_OP_AUTOEND(ret);
    return MEMCACHED_SUCCESS;
  }else{
    return MEMCACHED_FAILURE;
  }
}

memcached_return_t montage_remove(const char* key, size_t nkey, uint32_t exptime)
{
  return MEMCACHED_FAILURE;
}