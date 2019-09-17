/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include "pku_memcached.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "../data/generate_reqs.h"
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
int not_main(int argc, char **argv);
/*
static void get_string(int len, FILE *f, char* buff){
  for(int i = 0; i < len; ++i){
    fscanf(f, "%c", &buff[i]);
  }
  buff[len-1]=0;
  char throwaway;
  fscanf(f, "%c", &throwaway);
  assert(throwaway == '\n');
}
*/

int not_main(int argc, char **argv)
{
  int mytid = 0;
  char reciever[STR_LEN+5];
  int ret;
  unsigned long long begin, end, count = 0;

  FILE *f = fopen("../data/test.txt", "r");
  char key[STR_LEN+5], val[STR_LEN+5];
  int n;
  fscanf(f, "INSERT %d\n", &n); 
  for(int i = 0; i < n/2; ++i){ 
    fscanf(f, "%s %s", key, val);
    memcached_insert(key, STR_LEN, val, STR_LEN, 0, mytid);
  }
  for(int i = n/2; i < n; ++i){ 
    fscanf(f, "%s %s", key, val);
    begin = get_ticks_start();
    memcached_insert(key, STR_LEN, val, STR_LEN, 0, mytid);
    end = get_ticks_end();
    count += (end-begin);
  }
  printf("INSERT takes %fcy\n", ((count*1.0)/n*2));

  char command[STR_LEN];
  fscanf(f, "%s %d", command, &n);
  if (strcmp(command, "TOUCH") == 0){
    count = 0;
    printf("touched!\n");
    for(int i = 0; i < n/2; ++i){
      fscanf(f, "%s", key);
      memcached_touch(key, STR_LEN, 0, mytid);
    }
    for(int i = n/2; i < n; ++i){
      fscanf(f, "%s", key);
      begin = get_ticks_start();
      memcached_touch(key, STR_LEN, 0, mytid);
      end = get_ticks_end();
      count += (end-begin);
    }
    printf("TOUCH takes %fcy\n", ((count*1.0)/n*2));
    fscanf(f, "%s %d", command, &n);
  } else printf("command was %s\n", command);

  if (strcmp(command, "GET") == 0){
    printf("got!\n");
    count = 0;
    for(int i = 0; i < n/2; ++i){
      fscanf(f, "%s %s", key, val);
      ret = memcached_get(key, STR_LEN, 0, reciever, STR_LEN+5, mytid);
      if (ret){
	printf("error\n");
	return -1;
      }
    }
    for(int i = n/2; i < n; ++i){
      fscanf(f, "%s %s", key, val);
      begin = get_ticks_start();
      ret = memcached_get(key, STR_LEN, 0, reciever, STR_LEN+5, mytid);
      end = get_ticks_end();
      count += end - begin;
      if (ret){
	printf("error\n");
	return -1;
      }
    }
    printf("GET takes %fcy\n", (1.0*count)/n*2);
  } else printf("command was %s\n", command);
  memcached_end(mytid);
  printf("ran!\n");
  return ret;
}
