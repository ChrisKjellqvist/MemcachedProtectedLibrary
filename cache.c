/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "cache.h"

const int initial_pool_size = 64;

cache_t* cache_create(const char *name, size_t bufsize, size_t align,
                      cache_constructor_t* constructor,
                      cache_destructor_t* destructor) {
    cache_t* ret = calloc(1, sizeof(cache_t));
    char* nm = strdup(name);
    void** ptr = calloc(initial_pool_size, sizeof(void*));
    if (ret == NULL || nm == NULL || ptr == NULL ||
        pthread_mutex_init(&ret->mutex, NULL) == -1) {
        free(ret);
        free(nm);
        free(ptr);
        return NULL;
    }

    ret->name = nm;
    ret->ptr = ptr;
    ret->freetotal = initial_pool_size;
    ret->constructor = constructor;
    ret->destructor = destructor;

    ret->bufsize = bufsize;

    return ret;
}

static inline void* get_object(void *ptr) {
    return ptr;
}

void cache_destroy(cache_t *cache) {
    while (cache->freecurr > 0) {
        void *ptr = cache->ptr[--cache->freecurr];
        if (cache->destructor) {
            cache->destructor(get_object(ptr), NULL);
        }
        free(ptr);
    }
    free(cache->name);
    free(cache->ptr);
    pthread_mutex_destroy(&cache->mutex);
    free(cache);
}

void* cache_alloc(cache_t *cache) {
    void *ret;
    pthread_mutex_lock(&cache->mutex);
    ret = do_cache_alloc(cache);
    pthread_mutex_unlock(&cache->mutex);
    return ret;
}

void* do_cache_alloc(cache_t *cache) {
    void *ret;
    void *object;
    if (cache->freecurr > 0) {
        ret = cache->ptr[--cache->freecurr];
        object = get_object(ret);
    } else {
        object = ret = malloc(cache->bufsize);
        if (ret != NULL) {
            object = get_object(ret);

            if (cache->constructor != NULL &&
                cache->constructor(object, NULL, 0) != 0) {
                free(ret);
                object = NULL;
            }
        }
    }
    return object;
}

void cache_free(cache_t *cache, void *ptr) {
    pthread_mutex_lock(&cache->mutex);
    do_cache_free(cache, ptr);
    pthread_mutex_unlock(&cache->mutex);
}

void do_cache_free(cache_t *cache, void *ptr) {
    if (cache->freecurr < cache->freetotal) {
        cache->ptr[cache->freecurr++] = ptr;
    } else {
        /* try to enlarge free connections array */
        size_t newtotal = cache->freetotal * 2;
        void **new_free = realloc(cache->ptr, sizeof(char *) * newtotal);
        if (new_free) {
            cache->freetotal = newtotal;
            cache->ptr = new_free;
            cache->ptr[cache->freecurr++] = ptr;
        } else {
            if (cache->destructor) {
                cache->destructor(ptr, NULL);
            }
            free(ptr);

        }
    }
}

