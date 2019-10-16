CC = clang++
OBJECTS = plib.o memcached.o hash.o jenkins_hash.o murmur3_hash.o items.o \
	  assoc.o thread.o daemon.o stats.o util.o bipbuffer.o crawler.o \
	  itoa_ljust.o slab_automove.o slabs.o init.o testapp.o pku_memcached.o

OPT_LEVEL = -O0 -g -DNO_PKEY
#OPT_LEVEL = -O2
OPTS_LENIENT = -Iprotected/ -Iunprotected/ -Iplib/ -I../rpmalloc/src \
	       -DHAVE_CONFIG_H -MD -MP -Wall -std=c++17 $(OPT_LEVEL)
OPTS = $(OPTS_LENIENT) -Werror
# Default arguments that clang++ passes to the linker. This turns out to be
# important for C++ programs
DEFLINK  = --hash-style=gnu --no-add-needed --build-id --eh-frame-hdr -m elf_x86_64 -dynamic-linker /lib64/ld-linux-x86-64.so.2 -o a.out /usr/bin/../lib/gcc/x86_64-redhat-linux/8/../../../../lib64/crt1.o /usr/bin/../lib/gcc/x86_64-redhat-linux/8/../../../../lib64/crti.o /usr/bin/../lib/gcc/x86_64-redhat-linux/8/crtbegin.o -L/usr/bin/../lib/gcc/x86_64-redhat-linux/8 -L/usr/bin/../lib/gcc/x86_64-redhat-linux/8/../../../../lib64 -L/usr/bin/../lib64 -L/lib/../lib64 -L/usr/lib/../lib64 -L/usr/bin/../lib/gcc/x86_64-redhat-linux/8/../../.. -L/usr/bin/../lib -L/lib -L/usr/lib -lstdc++ -lm -lgcc_s -lgcc -lc -lgcc_s -lgcc /usr/bin/../lib/gcc/x86_64-redhat-linux/8/crtend.o /usr/bin/../lib/gcc/x86_64-redhat-linux/8/../../../../lib64/crtn.o 
LINKOPTS = $(DEFLINK) -lpthread -levent -T scripts/ldscript.lds

.PHONY : all
all: $(OBJECTS)
	ld $(LINKOPTS) -o exec $(OBJECTS) librpmalloc.a

%.o: protected/%.cc
	$(CC) -c $^ $(OPTS) -MT $@ -o $@

%.o: unprotected/%.cc
	$(CC) -c $^ $(OPTS) -MT $@ -o $@


.PHONY : clean
clean: 
	rm -f $(OBJECTS) exec *.d /dev/shm/memcached*
.PHONY : reset
reset:
	rm /dev/shm/memcached*
