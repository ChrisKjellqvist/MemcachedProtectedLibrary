#ifndef PLIB_HPP
#define PLIB_HPP

#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#define __set_errno(val) (errno = (val))

#define UINT64(x) ((unsigned long) x)
#define CAST64(x) ((unsigned long) x)

/* page size */
#define PGSHIFT         12              /* log2(PGSIZE) */
#define PGSIZE          (1 << PGSHIFT)  /* bytes mapped by a page */
#define PGMASK          (PGSIZE - 1)

/* offset in page */
#define PGOFF(la)       (((unsigned long) (la)) & PGMASK)
#define PGADDR(la)      (((unsigned long) (la)) & ~CAST64(PGMASK))

#define PSTACK_SIZE             0x800000

#define PKEY_DISABLE_NONE      		0x0
#define PKEY_DISABLE_ACCESS    		0x1
#define PKEY_DISABLE_WRITE		0x2

#define enable_access()						 	\
	__asm__ __volatile__ (					   	\
		"SET: \n\t"						\
		".byte 0x0f, 0x01, 0xef \n\t"				\
		"cmpl  $0xfffffff0, %%eax \n\t"				\
		"jne SET \n\t"						\
		: : "a" (0xfffffff0), "c" (0), "d" (0));

#define disable_access()					   	\
	__asm__ __volatile__ (					  	\
		"RESET: \n\t"						\
		".byte 0x0f, 0x01, 0xef \n\t"				\
		"cmpl  $0xfffffffc, %%eax \n\t"				\
		"jne RESET \n\t"					\
		: : "a" (0xfffffffc), "c" (0), "d" (0));

int pkey_set(int pkey, unsigned int rights);

int pkey_mprotect(void *addr, size_t len, int prot, int pkey);

int pkey_alloc(unsigned long flags, unsigned long access_rights);

int pkey_free(int pkey);

#endif
