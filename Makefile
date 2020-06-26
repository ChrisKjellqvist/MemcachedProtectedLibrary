CXX=g++
ALLOC=r
PROT_OBJ = obj/memcached.o obj/murmur3_hash.o obj/items.o obj/assoc.o obj/thread.o \
	   obj/bipbuffer.o obj/crawler.o obj/pku_memcached.o obj/util.o\
	   obj/itoa_ljust.o 

libralloc=ralloc/test

#OPT_LEVEL = -O0 -g
OPT_LEVEL = -O3

ERROR     = -DFAIL_ASSERT
OPTS = -Iinclude/  -levent -DHAVE_CONFIG_H -Wall -Werror -std=c++17 \
       -fPIC $(OPT_LEVEL) $(ERROR) -I./ralloc/src \
       -DUSE_HODOR -I./hodor/libhodor -I./hodor/include

LIBS = obj/libthreadcached.so ralloc/test/libralloc.a
LINKOPTS = -Lhodor/libhodor -lpthread -levent -ldl -ljemalloc #-lhodor
EXE = bin/server.exe bin/end.exe bin/get.exe bin/insert.exe

# Ralloc by default
ifeq ($(ALLOC),r)
	OPTS += -DRALLOC -L./ralloc/test -lralloc
	LINKOPTS += -L./ralloc/test -lralloc
endif

ifeq ($(ALLOC),mak)
	OPTS += -I./ralloc/ext/makalu_alloc/include -DMAKALU -L./ralloc/ext/makalu_alloc/lib -lmakalu
	LINKOPTS += -L./ralloc/ext/makalu_alloc/lib -lmakalu
endif

ifeq ($(ALLOC),je)
	OPTS += -DJEMALLOC
endif

ifeq ($(ALLOC),lr)
	# Ralloc without flush and fence is effectively LRMalloc, with optimization
	OPTS += -DRALLOC -DPWB_IS_NOOP -L./ralloc/test -lralloc
	LINKOPTS += -L./ralloc/test -lralloc
endif

ifeq ($(ALLOC),pmdk)
	OPTS += -DPMDK -lpmemobj
	LINKOPTS += -lpmemobj
endif


.PHONY : all lib bin
all: $(EXE) $(TEST_RUN)
lib: obj/libthreadcached.so
bin: server.exe
	mv $^ bin/memcached

%.exe: test/%.cc $(LIBS)
	$(CXX) $^ -o $@ $(OPTS) $(LINKOPTS)

obj/libthreadcached.so: $(PROT_OBJ)
	$(CXX) -shared $(PROT_OBJ) $(OPTS) -o $@ 

obj/%.o: src/%.cc
	$(CXX) -c $^ $(OPTS) -o $@

.PHONY : clean
clean: 
	rm -f obj/* exec *.d /dev/shm/memcached* $(EXE) $(TEST_RUN) $(PERF_RUN) $(RPMA_RUN)
.PHONY : reset
reset:
	@rm -f /dev/shm/test*
	@rm -f /dev/shm/memcached*
	@rm -f /mnt/pmem/test*
	@rm -f /mnt/pmem/memcached*

# include/hodo-plib.h
