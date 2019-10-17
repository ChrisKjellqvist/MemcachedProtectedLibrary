PROT_OBJ = obj/plib.o obj/memcached.o obj/hash.o obj/jenkins_hash.o \
	   obj/murmur3_hash.o obj/items.o obj/assoc.o obj/thread.o obj/daemon.o\
	   obj/stats.o obj/util.o obj/bipbuffer.o obj/crawler.o \
	   obj/itoa_ljust.o obj/slab_automove.o obj/slabs.o obj/pku_memcached.o 
UPRO_OBJ = obj/testapp.o 

OPT_LEVEL = -O0 -g
#OPT_LEVEL = -g -O2 -DNO_PKEY
OPTS_LENIENT = -Iprotected/ -Iunprotected/ -Iplib/ -I../rpmalloc/src \
	       -I../hodor/include -DHAVE_CONFIG_H -MD -MP -Wall -std=c++17 \
	       -fPIC -I../hodor/libhodor $(OPT_LEVEL)
OPTS = $(OPTS_LENIENT) -Werror
# Default arguments that clang++ passes to the linker. This turns out to be
# important for C++ programs
DEFLINK  = --hash-style=gnu --no-add-needed --build-id --eh-frame-hdr -m \
	   elf_x86_64 -dynamic-linker /lib64/ld-linux-x86-64.so.2 -o a.out \
	   /usr/bin/../lib/gcc/x86_64-redhat-linux/8/../../../../lib64/crt1.o\
	   /usr/bin/../lib/gcc/x86_64-redhat-linux/8/../../../../lib64/crti.o \
	   /usr/bin/../lib/gcc/x86_64-redhat-linux/8/crtbegin.o \
	   -L/usr/bin/../lib/gcc/x86_64-redhat-linux/8 \
	   -L/usr/bin/../lib/gcc/x86_64-redhat-linux/8/../../../../lib64 \
	   -L/usr/bin/../lib64 -L/lib/../lib64 -L/usr/lib/../lib64 \
	   -L/usr/bin/../lib/gcc/x86_64-redhat-linux/8/../../.. \
	   -L/usr/bin/../lib -L/lib -L/usr/lib -lstdc++ -lm -lgcc_s -lgcc -lc \
	   -lgcc_s -lgcc /usr/bin/../lib/gcc/x86_64-redhat-linux/8/crtend.o \
	   /usr/bin/../lib/gcc/x86_64-redhat-linux/8/../../../../lib64/crtn.o

LIBS = lib/libhodor.a lib/libthreadcached.so lib/librpmalloc.a
LINKOPTS = $(DEFLINK) -lpthread -levent -ldl -T scripts/ldscript.lds

.PHONY : all
all: $(LIBS) $(UPRO_OBJ)
	ld $(LINKOPTS) -o exec $(UPRO_OBJ) libhodor.a libthreadcached.so librpmalloc.a

lib/libhodor.a:
	cp ~/hodor/libhodor/libhodor.a .
lib/libthreadcached.so: $(PROT_OBJ)
	$(CC) -shared $(PROT_OBJ) $(OPTS) -o libthreadcached.so

lib/librpmalloc.a:
	cp ~/rpmalloc/librpmalloc.a .

obj/%.o: protected/%.cc
	$(CC) -c $^ $(OPTS) -MT $@ -o $@

obj/%.o: unprotected/%.cc
	$(CC) -c $^ $(OPTS) -MT $@ -o $@


.PHONY : clean
clean: 
	rm -f $(PROT_OBJ) $(UPRO_OBJ) exec *.d /dev/shm/memcached*
.PHONY : reset
reset:
	rm -f /dev/shm/memcached*

# hodor/apps/kv/app/Makefile
# include/hodo-plib.h
# localdisk/qemu-clean.img -- for vm
