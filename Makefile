CXX=g++
ALLOC=r
PROT_OBJ=./Montage/obj/EpochSys.o ./Montage/obj/Recorder.o ./Montage/obj/TestConfig.o\
		  ./Montage/obj/ParallelLaunch.o ./Montage/obj/CustomTypes.o ./Montage/obj/HarnessUtils.o\
			obj/memcached.o obj/montage_memcached.o obj/murmur3_hash.o obj/items.o obj/assoc.o obj/thread.o \
	   obj/bipbuffer.o obj/crawler.o obj/pku_memcached.o obj/util.o obj/itoa_ljust.o 
libralloc=../ext/ralloc/test

OPT_LEVEL = -O0 -g
#OPT_LEVEL = -O3

ERROR = -DFAIL_ASSERT
# memcacheD header file path
OPTS = -Iinclude/  -levent -DHAVE_CONFIG_H -Wall -std=c++17 \
       -fPIC $(OPT_LEVEL) $(ERROR) -I../ext/ralloc/src \
#       -DUSE_HODOR -I./hodor/libhodor -I./hodor/include
# Montage header file path
OPTS += -I./Montage -I./Montage/utils -I./Montage/persist -I./Montage/api -I./Montage/test
# tbb header file path
OPTS += -I../ext/tbb/include

LIBS = obj/libthreadcached.so ../ext/ralloc/libralloc.a
LINKOPTS = -Lhodor/libhodor -lpthread -levent -ldl -ljemalloc #-lhodor
EXE = bin/server bin/end bin/get bin/insert bin/MontageMemcacheD

# Ralloc by default
ifeq ($(ALLOC),r)
	OPTS+=-DRALLOC -L../ext/ralloc -lralloc
	LINKOPTS += -L../ext/ralloc -lralloc -lhwloc
endif

ifeq ($(ALLOC),mak)
	OPTS+=-I../ext/ralloc/ext/makalu_alloc/include -DMAKALU -L../ext/ralloc/ext/makalu_alloc/lib -lmakalu
	LINKOPTS += -L../ext/ralloc/ext/makalu_alloc/lib -lmakalu
endif

ifeq ($(ALLOC),je)
	OPTS += -DJEMALLOC
endif

ifeq ($(ALLOC),lr)
	# Ralloc without flush and fence is effectively LRMalloc, with optimization
	OPTS+=-DRALLOC -DPWB_IS_NOOP -L../ext/ralloc/test -lralloc
	LINKOPTS += -L../ext/ralloc/test -lralloc
endif

ifeq ($(ALLOC),pmdk)
	OPTS += -DPMDK -lpmemobj
	LINKOPTS += -lpmemobj
endif

.PHONY : all lib bin
all: $(EXE) $(TEST_RUN)
lib: obj/libthreadcached.so
bin: server
	mv $^ bin/memcached

Montage/obj/%.o : ./Montage/%.cpp
	$(CXX) -c $^ $(OPTS) -o $@

Montage/obj/%.o : ./Montage/utils/%.cpp 
	$(CXX) -c $^ $(OPTS) -o $@

Montage/obj/%.o : ./Montage/persist/%.cpp 
	$(CXX) -c $^ $(OPTS) -o $@

bin/%: test/%.cc $(LIBS)
	$(CXX) $^ -o $@ $(OPTS) $(LINKOPTS)

obj/%.o: src/%.cc
	$(CXX) -c $^ $(OPTS) -o $@

obj/libthreadcached.so: $(PROT_OBJ)
	$(CXX) -shared $(PROT_OBJ) $(OPTS) -o $@ 

.PHONY : clean
clean: 
	rm -f obj/* Montage/obj/* exec *.d /dev/shm/memcached* $(EXE) $(TEST_RUN) $(PERF_RUN) $(RPMA_RUN) bin/*
.PHONY : reset
reset:
	@rm -f /dev/shm/test*
	@rm -f /dev/shm/memcached*
	@rm -f /mnt/pmem/test*
	@rm -f /mnt/pmem/memcached*

# include/hodo-plib.h