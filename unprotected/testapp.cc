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

/*
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
*/

const int buff_len = 32;

int main(){
  memcached_init();
// Necessary until we start testing Hodor. Mohammad's code should make everything easy :) 
//  hodor_init();
  char buff[buff_len];
  char age[buff_len];
  char check_age[buff_len+2];
  strcpy(buff, std::string("chris").c_str());
  strcpy(age, std::string("21").c_str());
  memcached_insert(buff, strlen(buff), 0, age, strlen(age), 0);  
  printf("insert worked\n");

  memset(check_age, 0, buff_len);
  memcached_get(buff, strlen(buff), 0, check_age, buff_len, 0); 
  printf("%s's age is %s\n", buff, check_age);

  /*
  exit_code = 141;
  memcached_touch(buff, strlen(buff), 0, 0);
*/
  memcached_end(0);
  return 0;
}
