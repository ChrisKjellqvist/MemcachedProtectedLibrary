CC = g++
PROT_OBJ = obj/memcached.o\
	   obj/murmur3_hash.o obj/items.o obj/assoc.o obj/thread.o \
	   obj/bipbuffer.o obj/crawler.o obj/slabs.o \
	   obj/slab_automove.o obj/pku_memcached.o obj/util.o\
	   obj/itoa_ljust.o 

SERV_OBJ = obj/memcached.o\
	   obj/murmur3_hash.o obj/items.o obj/assoc.o obj/thread.o \
	   obj/bipbuffer.o obj/crawler.o obj/slabs.o \
	   obj/slab_automove.o obj/util.o obj/itoa_ljust.o 

libralloc=ralloc/test
libhodor= hodor/libhodor

#OPT_LEVEL = -O1 -g
OPT_LEVEL = -O3 -g
ERROR     = -DFAIL_ASSERT
OPTS_LENIENT = -Iinclude/ -Iralloc/src \
	       -Ihodor/include -DHAVE_CONFIG_H -MD -MP -Wall -std=c++17 \
	       -fPIC -I$(libhodor) $(OPT_LEVEL) $(ERROR)
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

LIBS = $(libhodor)/libhodor.a obj/libthreadcached.so $(libralloc)/libralloc.a
LINKOPTS = $(DEFLINK) -lpthread -levent -ldl -T scripts/ldscript.lds
EXE = bin/server.exe bin/end.exe
TEST_RUN = bin/get.exe bin/insert.exe bin/timed_get.exe
PERF_RUN = bin/insert_test.exe bin/get_test.exe
RPMA_RUN = bin/basic_setup.exe bin/basic_test.exe

.PHONY : perf all lib bin install
perf: $(EXE) $(PERF_RUN)
all: $(EXE) $(TEST_RUN)
lib: obj/libthreadcached.so
bin: bin/server.exe
	mv $^ bin/memcached

bin/%.exe: $(LIBS) obj/%.o
	ld $(LINKOPTS) $^ -o $@
$(libhodor)/libhodor.a:
	make -C $(libhodor) libhodor.a
obj/libthreadcached.so: $(PROT_OBJ)
	$(CC) -shared $(PROT_OBJ) $(OPTS) -o $@ 
$(libralloc)/libralloc.a:
	$(MAKE) -C ralloc/test libralloc.a

obj/%.o: protected/%.cc
	$(CC) -c $^ $(OPTS) -MT $@ -o $@

obj/%.o: unprotected/%.cc
	$(CC) -c $^ $(OPTS) -MT $@ -o $@


.PHONY : clean
clean: 
	rm -f obj/* exec *.d /dev/shm/memcached* $(EXE) $(TEST_RUN) $(PERF_RUN) $(RPMA_RUN)
	make -C $(libhodor) clean
	make -C $(libralloc) clean
.PHONY : reset
reset:
	rm -f /dev/shm/memcached*

# hodor/apps/kv/app/Makefile
# include/hodo-plib.h
# localdisk/qemu-clean.img -- for vm
