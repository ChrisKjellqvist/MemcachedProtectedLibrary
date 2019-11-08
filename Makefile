CC = g++
PROT_OBJ = obj/memcached.o\
	   obj/murmur3_hash.o obj/items.o obj/assoc.o obj/thread.o \
	   obj/bipbuffer.o obj/crawler.o obj/slabs.o \
	   obj/slab_automove.o obj/pku_memcached.o 

OPT_LEVEL = -O0 -g
#OPT_LEVEL =-O2
OPTS_LENIENT = -Iprotected/ -Iunprotected/ -I../rpmalloc/src \
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
EXE = server client

.PHONY : all
all: $(EXE)
server: $(LIBS) obj/server.o
	ld $(LINKOPTS) -o server obj/server.o $(LIBS)
client: $(LIBS) obj/testapp.o
	ld $(LINKOPTS) -o client obj/testapp.o $(LIBS)
lib/libhodor.a:
	cp ~/hodor/libhodor/libhodor.a lib/
lib/libthreadcached.so: $(PROT_OBJ)
	$(CC) -shared $(PROT_OBJ) $(OPTS) -o lib/libthreadcached.so

lib/librpmalloc.a:
	cp ~/rpmalloc/librpmalloc.a lib/

obj/%.o: protected/%.cc
	$(CC) -c $^ $(OPTS) -MT $@ -o $@

obj/%.o: unprotected/%.cc
	$(CC) -c $^ $(OPTS) -MT $@ -o $@


.PHONY : clean
clean: 
	rm -f obj/* exec *.d /dev/shm/memcached* $(EXE)
.PHONY : reset
reset:
	rm -f /dev/shm/memcached*

# hodor/apps/kv/app/Makefile
# include/hodo-plib.h
# localdisk/qemu-clean.img -- for vm
