CXX=g++
PROT_OBJ = obj/memcached.o\
	   obj/murmur3_hash.o obj/items.o obj/assoc.o obj/thread.o \
	   obj/bipbuffer.o obj/crawler.o \
	   obj/pku_memcached.o obj/util.o\
	   obj/itoa_ljust.o 

SERV_OBJ = obj/memcached.o\
	   obj/murmur3_hash.o obj/items.o obj/assoc.o obj/thread.o \
	   obj/bipbuffer.o obj/crawler.o \
	   obj/util.o obj/itoa_ljust.o 

libralloc=ralloc/test

# OPT_LEVEL = -O0 -g
OPT_LEVEL = -O3 -g
ERROR     = -DFAIL_ASSERT
OPTS = -Iinclude/  -levent\
	       -DHAVE_CONFIG_H -Wall -Werror \
	       -std=c++17 -fPIC $(OPT_LEVEL) $(ERROR) \
		   -I./ralloc/src 

LIBS = obj/libthreadcached.a
LINKOPTS = -lpthread -levent -ldl -ljemalloc 
EXE = bin/server.exe bin/end.exe
TEST_RUN = bin/get.exe bin/insert.exe
PERF_RUN = bin/insert_test.exe bin/get_test.exe
RPMA_RUN = bin/basic_setup.exe bin/basic_test.exe

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



.PHONY : perf all lib bin install
perf: $(EXE) $(PERF_RUN)
all: $(EXE) $(TEST_RUN)
lib: obj/libthreadcached.a
bin: bin/server.exe
	mv $^ bin/memcached

bin/%.exe: obj/%.o $(LIBS) 
	$(CXX) $^ -o $@ $(LINKOPTS)
obj/libthreadcached.a: $(PROT_OBJ)
	ar -rcs $@ $^ 
# $(libralloc)/libralloc.a:
# 	$(MAKE) -C ralloc/test libralloc.a

obj/%.o: protected/%.cc
	$(CXX) -c $^ $(OPTS) -o $@

obj/%.o: unprotected/%.cc
	$(CXX) -c $^ $(OPTS) -o $@


.PHONY : clean
clean: 
	rm -f obj/* exec *.d /dev/shm/memcached* $(EXE) $(TEST_RUN) $(PERF_RUN) $(RPMA_RUN)
	# make -C $(libralloc) clean
.PHONY : reset
reset:
	rm -f /dev/shm/test*

# include/hodo-plib.h
