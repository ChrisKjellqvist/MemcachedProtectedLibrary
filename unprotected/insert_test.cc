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

static inline void cpuid(void) {
  asm volatile(
      "cpuid\n\t"
      : ::"%rax", "%rbx", "%rcx", "%rdx");
}

static inline unsigned long get_ticks_start(void) {
  unsigned int cycles_high, cycles_low;
  asm volatile(
      "cpuid\n\t"
      "rdtsc\n\t"
      "mov %%edx, %0\n\t"
      "mov %%eax, %1\n\t"
      : "=r"(cycles_high), "=r"(cycles_low)::"%rax", "%rbx", "%rcx", "%rdx");
  return ((unsigned long)cycles_low) | (((unsigned long)cycles_high) << 32);
}

static inline unsigned long get_ticks_end(void) {
  unsigned int cycles_high, cycles_low;
  asm volatile(
      "rdtscp\n\t"
      "mov %%edx, %0\n\t"
      "mov %%eax, %1\n\t"
      "cpuid\n\t"
      : "=r"(cycles_high), "=r"(cycles_low)::"%rax", "%rbx", "%rcx", "%rdx");
  return ((unsigned long)cycles_low) | (((unsigned long)cycles_high) << 32);
}


#define HMAP_SIZ 600000
#define N_INSERT 100000
#define N_GET    100000
#define BUFF_LEN 64
#define GET_NS(cy, N) ((cy*1.0)/2.2/N)
#define GET_RAND_R(MAX) (rand() % MAX)

class packet {
  public:
    size_t key_len;
    int  *key;
    size_t dat_len;
    int  *dat;
};

// max key length is 128 bytes, 0x1 -> 0x3F
// max dat length is 128kB, 0x20 -> 0x1FFFF
#define getKeyLen() (rand() & 0x7F) | 7
//#define getDatLen() (rand() & 0x1FFFF) | 0x20
#define getDatLen() (rand() & 0xFF) | 7
// rand() in our implementation returns 31 bytes of data, get that 32nd in there
#define getRandomDat() (rand() << 1) | (rand() & 1)

packet* create_random_packet(){
  auto p = new packet();
  p->key_len = getKeyLen();
  p->dat_len = getDatLen();
  p->key = (int*)malloc(p->key_len);
  p->dat = (int*)malloc(p->dat_len);
  for(unsigned i = 0; i < (p->key_len >> 2); ++i)
    p->key[i] = getRandomDat();
  for(unsigned i = 0; i < (p->dat_len >> 2); ++i)
    p->dat[i] = getRandomDat();
  return p;
}

int main(){
  srand(71798);
  memcached_init(0);
  unsigned long long begin, end, count = 0;
  const int n = 100000;
  size_t nkB = 0;
  for(unsigned i = 0; i < 1000; ++i){
    auto p = create_random_packet();
    memcached_insert((char*)p->key, p->key_len, 0, (char*)p->dat, p->dat_len);
  }
  for(unsigned i = 0; i < n; ++i){
    auto p = create_random_packet();
    nkB += (p->key_len + p->dat_len);
    begin = get_ticks_start();
    memcached_insert((char*)p->key, p->key_len, 0, (char*)p->dat, p->dat_len);
    end = get_ticks_end();
    count += end - begin;
  }
  printf("%lukB\n", nkB/1024);
  printf("%fns\n", GET_NS(count, n));
}
