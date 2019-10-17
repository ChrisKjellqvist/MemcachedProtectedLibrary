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

char *write_random_string(char* wr){
  for(unsigned i = 0 ;i < BUFF_LEN - 5; ++i)
    wr[i] = rand() | 0x81;
  wr[BUFF_LEN-5] = 0;
  return wr;
}
char *keys[N_INSERT];
char *dats[N_INSERT];

int main(){
  for(unsigned i = 0; i < N_INSERT; ++i)
    keys[i] = dats[i] = nullptr;

  memcached_init();
  srand(42);
// Necessary until we start testing Hodor. Mohammad's code should make everything easy :) 
//  hodor_init();


  unsigned long long count = 0, start, end;

  for(unsigned i = 0; i < N_INSERT; ++i){
    keys[i] = write_random_string((char*)malloc(BUFF_LEN));
    dats[i] = write_random_string((char*)malloc(BUFF_LEN));
    start = get_ticks_start();
    memcached_insert(keys[i], BUFF_LEN - 5, 0, dats[i], BUFF_LEN - 5, 0);
    end = get_ticks_end();
    count += end - start;
  }
  printf("insert in %fns\n", GET_NS(count, N_INSERT)); 

  count = 0;
  for(unsigned i = 0; i < N_GET; ++i){
    char buff[BUFF_LEN];
    unsigned idx = GET_RAND_R(N_INSERT);
    start = get_ticks_start();
    int res = memcached_get(keys[idx], BUFF_LEN - 5, 0, buff, BUFF_LEN, 0);  
    end = get_ticks_end();
    count += end - start;
    assert(res == 0 && "Get right value");
    buff[BUFF_LEN-5] = 0;
    assert(strcmp(buff, dats[idx]) == 0);
  }
  printf("get in %fns\n", GET_NS(count, N_GET));
  

  memcached_end(0);
  return 0;
}
