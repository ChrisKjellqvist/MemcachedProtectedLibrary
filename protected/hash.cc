/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */

#include "memcached.h"
#include "jenkins_hash.h"
#include "murmur3_hash.h"
#include "hash.h"

hash_func tcd_hash;
int hash_init(enum hashfunc_type type) {
    switch(type) {
        case JENKINS_HASH:
            tcd_hash = jenkins_hash;
            settings.hash_algorithm = "jenkins";
            break;
        case MURMUR3_HASH:
            tcd_hash = MurmurHash3_x86_32;
            settings.hash_algorithm = "murmur3";
            break;
        default:
            return -1;
    }
    return 0;
}
