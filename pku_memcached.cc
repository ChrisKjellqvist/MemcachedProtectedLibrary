#include "pku_memcached.h"
#include <stdlib.h>
#include "memcached.h"
#include <plib.h>

extern int pkey;
extern pthread_mutex_t end_mutex;
extern pthread_mutex_t begin_ops_mutex;
static unsigned long* t_psp_ar[256];
// TODO get imports

asm("trampoline_exit: ret");

static unsigned long *get_new_stack(){
  char *ptr = (char*)mmap(0, PSTACK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (ptr == (char*)-1){
    printf("error!\n");
    exit(1);
  }
  for(char* p = ptr; p < ptr + PSTACK_SIZE; ++p)
    *p = 0;
  ptr += PSTACK_SIZE;
  return (unsigned long*)ptr;
}
void memcached_touch(char* key, size_t nkey, uint32_t exptime, int t_id){

  unsigned long sp;
  unsigned long *psp = t_psp_ar[t_id];
  if (unlikely(psp == NULL)){
    t_psp_ar[t_id] = get_new_stack();
    psp = t_psp_ar[t_id];
  }
  struct touch_struct ts;
  ts.k = key; ts.nk = nkey; ts.exptime = exptime;
#ifndef NO_PKEY
  pkey_set(pkey, PKEY_DISABLE_NONE);
#endif
  asm("mov %%rsp, %0" : "=rm" (sp));
  *(psp - 1) = sp;
  asm("mov %0, %%rsp" : : "r" (psp - 1));
  // TODO this needs to be dealt with as well since r will be on stack
  pku_memcached_touch(&ts);

  asm("pop %%rsp" ::);
#ifndef NO_PKEY
  pkey_set(pkey, PKEY_DISABLE_ACCESS);
#endif
  asm("jmp trampoline_exit");
}

void memcached_insert(char* key, size_t nkey, char* data, size_t datan, uint32_t exptime, int t_id){ 
  unsigned long sp;
  unsigned long *psp = t_psp_ar[t_id];
  if (unlikely(psp == NULL)){
    t_psp_ar[t_id] = get_new_stack();
    psp = t_psp_ar[t_id];
  }
  struct insert_struct is;
  is.k = key; is.nk = nkey; is.d = data; is.dn = datan;
  is.exptime = exptime;
#ifndef NO_PKEY
  pkey_set(pkey, PKEY_DISABLE_NONE);
#endif
  asm("mov %%rsp, %0" : "=rm" (sp));
  *(psp - 1) = sp;
  asm("mov %0, %%rsp" : : "r" (psp - 1));

  pku_memcached_insert(&is);

  asm("pop %%rsp" ::);
#ifndef NO_PKEY
  pkey_set(pkey, PKEY_DISABLE_ACCESS);
#endif
  asm("jmp trampoline_exit");
}

void memcached_get(char* key, size_t nkey, uint32_t exptime, char* buffer, size_t buffLen, int *exit_code, int t_id){
  unsigned long sp;
  unsigned long *psp = t_psp_ar[t_id];
  if (unlikely(psp == NULL)){
    t_psp_ar[t_id] = get_new_stack();
    psp = t_psp_ar[t_id];
  }
  struct get_struct gs;
  gs.k = key; gs.nk = nkey; gs.exptime = exptime;
  gs.buffLen = buffLen; gs.buffer = buffer;
#ifndef NO_PKEY
  pkey_set(pkey, PKEY_DISABLE_NONE);
#endif
  asm("mov %%rsp, %0" : "=rm" (sp));
  *(psp - 1) = sp;
  asm("mov %0, %%rsp" : : "r" (psp - 1));
  int ret = pku_memcached_get(&gs);
  asm("pop %%rsp" ::);
#ifndef NO_PKEY
  pkey_set(pkey, PKEY_DISABLE_ACCESS);
#endif
  *exit_code = ret;
  asm("jmp trampoline_exit");
}

int start_server_thread(pthread_t *thread, int argc, char** argv){
  pthread_mutex_lock(&end_mutex);
  pthread_mutex_lock(&begin_ops_mutex);
  struct pthread_args *my_args = (struct pthread_args*)malloc(sizeof(struct pthread_args));
  my_args->argc = argc;
  my_args->argv = argv;
  int ret = pthread_create(thread, NULL, server_thread, my_args);
  pthread_mutex_lock(&begin_ops_mutex);
  return ret;
}
void memcached_end(int t_id){
  unsigned long sp;
  unsigned long *psp = t_psp_ar[t_id];
  if (unlikely(psp == NULL)){
    t_psp_ar[t_id] = get_new_stack();
    psp = t_psp_ar[t_id];
  }
#ifndef NO_PKEY
  pkey_set(pkey, PKEY_DISABLE_NONE);
#endif
  asm("mov %%rsp, %0" : "=rm" (sp));
  *(psp - 1) = sp;
  asm("mov %0, %%rsp" : : "r" (psp - 1));

  // switch our stack to the protected stack

  // do what we're here for
  pthread_mutex_unlock(&end_mutex);

  // restore our stack

  asm("pop %%rsp" ::);
#ifndef NO_PKEY
  pkey_set(pkey, PKEY_DISABLE_ACCESS);
#endif
  asm("jmp trampoline_exit");
}

