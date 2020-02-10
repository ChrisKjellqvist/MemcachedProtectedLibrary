CXX=g++
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
OPTS = -Iinclude/ -Iralloc/src -levent\
	       -Ihodor/include -DHAVE_CONFIG_H -Wall -Werror \
	       -std=c++17 -fPIC -I$(libhodor) $(OPT_LEVEL) $(ERROR)

LIBS = $(libhodor)/libhodor.a obj/libthreadcached.so $(libralloc)/libralloc.a
LINKOPTS = -lpthread -levent -ldl
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
	$(CXX) $(LINKOPTS) $^ -o $@
$(libhodor)/libhodor.a:
	make -C $(libhodor) libhodor.a
obj/libthreadcached.so: $(PROT_OBJ)
	$(CXX) -shared $(PROT_OBJ) $(OPTS) -o $@ 
$(libralloc)/libralloc.a:
	$(MAKE) -C ralloc/test libralloc.a

obj/%.o: protected/%.cc
	$(CXX) -c $^ $(OPTS) -o $@

obj/%.o: unprotected/%.cc
	$(CXX) -c $^ $(OPTS) -o $@


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
