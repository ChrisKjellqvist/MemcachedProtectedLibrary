#include "plib.h" 
#include <stdio.h>
#include <errno.h>
/* Return the value of the PKRU register.  */
static unsigned int
pkey_read(void) {
  unsigned int result;
  __asm__ volatile (".byte 0x0f, 0x01, 0xee"
      : "=a" (result) : "c" (0) : "rdx");
  return result;
}

/* Overwrite the PKRU register with VALUE.  */
static void
pkey_write(unsigned int value) {
  __asm__ volatile (".byte 0x0f, 0x01, 0xef"
      : : "a" (value), "c" (0), "d" (0));
}

int Hodor::pkey_set(int key, unsigned int rights) {
  if (key < 0 || key > 15 || rights > 3) {
    __set_errno (EINVAL);
    return -1;
  }
  unsigned int mask = 3 << (2 * key);
  unsigned int pkru = pkey_read ();
  pkru = (pkru & ~mask) | (rights << (2 * key));
  pkey_write (pkru);
  return 0;
}

int Hodor::pkey_mprotect(void *addr, size_t len, int prot, int pkey) {
  return syscall(329, addr, len, prot, pkey);
}

int Hodor::pkey_alloc(unsigned long flags, unsigned long access_rights) {
  return syscall(330, flags, access_rights);
}
int Hodor::pkey_free(int pkey) {
  return syscall(331, pkey);
}
