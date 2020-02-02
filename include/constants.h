/* LibMemcached
 * Copyright (C) 2006-2009 Brian Aker
 * All rights reserved.
 *
 * Use and distribution licensed under the BSD license.  See
 * the COPYING file in the parent directory for full text.
 *
 * Summary: Constants for libmemcached
 *
 */

#ifndef __LIBMEMCACHED_CONSTANTS_H__
#define __LIBMEMCACHED_CONSTANTS_H__

/* Public defines */
#define MEMCACHED_DEFAULT_PORT 11211
#define MEMCACHED_MAX_KEY 251 /* We add one to have it null terminated */
#define MEMCACHED_MAX_BUFFER 8196
#define MEMCACHED_MAX_HOST_SORT_LENGTH 86 /* Used for Ketama */
#define MEMCACHED_POINTS_PER_SERVER 100
#define MEMCACHED_POINTS_PER_SERVER_KETAMA 160
#define MEMCACHED_CONTINUUM_SIZE MEMCACHED_POINTS_PER_SERVER*100 /* This would then set max hosts to 100 */
#define MEMCACHED_STRIDE 4
#define MEMCACHED_DEFAULT_TIMEOUT 1000
#define MEMCACHED_CONTINUUM_ADDITION 10 /* How many extra slots we should build for in the continuum */
#define MEMCACHED_PREFIX_KEY_MAX_SIZE 128
#define MEMCACHED_EXPIRATION_NOT_ADD 0xffffffffU
#define MEMCACHED_VERSION_STRING_LENGTH 24


typedef enum {
  MEMCACHED_SUCCESS,
  MEMCACHED_FAILURE,
  MEMCACHED_HOST_LOOKUP_FAILURE,
  MEMCACHED_CONNECTION_FAILURE,
  MEMCACHED_CONNECTION_BIND_FAILURE,
  MEMCACHED_WRITE_FAILURE,
  MEMCACHED_READ_FAILURE,
  MEMCACHED_UNKNOWN_READ_FAILURE,
  MEMCACHED_PROTOCOL_ERROR,
  MEMCACHED_CLIENT_ERROR,
  MEMCACHED_SERVER_ERROR,
  MEMCACHED_CONNECTION_SOCKET_CREATE_FAILURE,
  MEMCACHED_DATA_EXISTS,
  MEMCACHED_DATA_DOES_NOT_EXIST,
  MEMCACHED_NOTSTORED,
  MEMCACHED_STORED,
  MEMCACHED_NOTFOUND,
  MEMCACHED_MEMORY_ALLOCATION_FAILURE,
  MEMCACHED_PARTIAL_READ,
  MEMCACHED_SOME_ERRORS,
  MEMCACHED_NO_SERVERS,
  MEMCACHED_END,
  MEMCACHED_DELETED,
  MEMCACHED_VALUE,
  MEMCACHED_STAT,
  MEMCACHED_ITEM,
  MEMCACHED_ERRNO,
  MEMCACHED_FAIL_UNIX_SOCKET,
  MEMCACHED_NOT_SUPPORTED,
  MEMCACHED_NO_KEY_PROVIDED, /* Deprecated. Use MEMCACHED_BAD_KEY_PROVIDED! */
  MEMCACHED_FETCH_NOTFINISHED,
  MEMCACHED_TIMEOUT,
  MEMCACHED_BUFFERED,
  MEMCACHED_BAD_KEY_PROVIDED,
  MEMCACHED_INVALID_HOST_PROTOCOL,
  MEMCACHED_SERVER_MARKED_DEAD,
  MEMCACHED_UNKNOWN_STAT_KEY,
  MEMCACHED_E2BIG,
  MEMCACHED_INVALID_ARGUMENTS,
  MEMCACHED_KEY_TOO_BIG,
  MEMCACHED_MAXIMUM_RETURN /* Always add new error code before */
} memcached_return_t;

enum delta_result_type {
  OK, 
  NON_NUMERIC, 
  EOM, 
  DELTA_ITEM_NOT_FOUND, 
  DELTA_ITEM_CAS_MISMATCH
};

// we never use memcached_st
#ifdef __cplusplus
extern "C" {
#endif
  typedef struct {
    bool a;
  } memcached_st;

  typedef struct {
    uint32_t item_flags;
    time_t item_expiration;
    size_t key_length;
    uint64_t item_cas;
    char *data;
    uint32_t datan;
    char* key;
    uint32_t keyn;
  } memcached_result_st;
#ifdef __cplusplus
}
#endif
#endif /* __LIBMEMCACHED_CONSTANTS_H__ */
