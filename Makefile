CC = g++
PROT_OBJ = obj/memcached.o\
	   obj/murmur3_hash.o obj/items.o obj/assoc.o obj/thread.o \
	   obj/bipbuffer.o obj/crawler.o obj/slabs.o \
	   obj/slab_automove.o obj/pku_memcached.o obj/util.o\
	   obj/itoa_ljust.o 

#OPT_LEVEL = -O0 -g
ERROR     = -DFAIL_ASSERT
OPT_LEVEL =-Ofast
OPTS_LENIENT = -Iprotected/ -Iunprotected/ -I../rpmalloc/src \
	       -I../hodor/include -DHAVE_CONFIG_H -MD -MP -Wall -std=c++17 \
	       -fPIC -I../hodor/libhodor -D__cplus_plus $(OPT_LEVEL) $(ERROR)
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
EXE = server.exe end.exe
TEST_RUN = get.exe insert.exe
PERF_RUN = insert_test.exe get_test.exe
RPMA_RUN = basic_setup.exe basic_test.exe

.PHONY : perf
perf: $(EXE) $(PERF_RUN)
.PHONY : all
all: $(EXE) $(TEST_RUN)
%.exe: $(LIBS) obj/%.o
	ld $(LINKOPTS) $^ -o $@
lib/libhodor.a:
	cp ~/hodor/libhodor/libhodor.a lib/
lib/libthreadcached.so: $(PROT_OBJ)
	$(CC) -shared $(PROT_OBJ) $(OPTS) -o lib/libthreadcached.so

lib/librpmalloc.a:
	$(MAKE) librpmalloc.a -C ~/rpmalloc/test
	cp ~/rpmalloc/test/librpmalloc.a $@

obj/%.o: protected/%.cc
	$(CC) -c $^ $(OPTS) -MT $@ -o $@

obj/%.o: unprotected/%.cc
	$(CC) -c $^ $(OPTS) -MT $@ -o $@


.PHONY : clean
clean: 
	rm -f obj/* exec *.d /dev/shm/memcached* $(EXE) $(TEST_RUN) $(PERF_RUN) $(RPMA_RUN)
.PHONY : reset
reset:
	rm -f /dev/shm/memcached*

# hodor/apps/kv/app/Makefile
# include/hodo-plib.h
# localdisk/qemu-clean.img -- for vm
