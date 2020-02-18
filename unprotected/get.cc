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
  memcached_init();
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
  printf("\n");

  return 0;
}
