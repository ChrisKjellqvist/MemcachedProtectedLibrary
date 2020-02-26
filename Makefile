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

#OPT_LEVEL = -O0 -g
OPT_LEVEL = -O3
ERROR     = -DFAIL_ASSERT
OPTS = -Iinclude/ -Iralloc/src -levent\
	       -DHAVE_CONFIG_H -Wall -Werror \
	       -std=c++17 -fPIC $(OPT_LEVEL) $(ERROR)

LIBS = obj/libthreadcached.so $(libralloc)/libralloc.a
LINKOPTS = -lpthread -levent -ldl
EXE = bin/server.exe bin/end.exe
TEST_RUN = bin/get.exe bin/insert.exe
PERF_RUN = bin/insert_test.exe bin/get_test.exe
RPMA_RUN = bin/basic_setup.exe bin/basic_test.exe

.PHONY : perf all lib bin install
perf: $(EXE) $(PERF_RUN)
all: $(EXE) $(TEST_RUN)
lib: obj/libthreadcached.so
bin: bin/server.exe
	mv $^ bin/memcached

bin/%.exe: $(LIBS) obj/%.o
	$(CXX) $^ -o $@ $(LINKOPTS)
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
	make -C $(libralloc) clean
.PHONY : reset
reset:
	sudo rm -f /dev/shm/memcached*

# include/hodo-plib.h
