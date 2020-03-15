CXX=g++
PROT_OBJ = obj/memcached.o\
	   obj/murmur3_hash.o obj/items.o obj/assoc.o obj/thread.o \
	   obj/bipbuffer.o obj/crawler.o \
	   obj/pku_memcached.o obj/util.o\
	   obj/itoa_ljust.o 

SERV_OBJ = obj/memcached.o\
	   obj/murmur3_hash.o obj/items.o obj/assoc.o obj/thread.o \
	   obj/bipbuffer.o obj/crawler.o  \
	   obj/util.o obj/itoa_ljust.o 

libralloc=ralloc/test
libhodor=hodor/libhodor/

#OPT_LEVEL = -O0 -g
OPT_LEVEL = -O3
ERROR     = -DFAIL_ASSERT
OPTS = -Iinclude/ -Iralloc/src -levent -m64 \
	       -Ihodor/include -DHAVE_CONFIG_H -Wall -Werror \
	       -std=c++17 -fPIC -I$(libhodor) $(OPT_LEVEL) $(ERROR)

LIBS = obj/libthreadcached.so $(libralloc)/libralloc.a
LINKOPTS = $(libralloc)/libralloc.a obj/libthreadcached.so -L$(libhodor) -lhodor -ldl -lpthread
EXE = bin/server.exe bin/end.exe bin/get.exe bin/insert.exe

.PHONY : all lib bin install
all: $(LIBS) $(EXE)
lib: obj/libthreadcached.so
bin: bin/server.exe
	mv $^ bin/memcached

$(libralloc)/libralloc.a:
	$(MAKE) -C ralloc/test libralloc.a

bin/%.exe: $(LIBS) obj/%.o
	$(CXX) $^ -o $@ $(LINKOPTS)

obj/libthreadcached.so: $(PROT_OBJ)
	$(CXX) -shared $(PROT_OBJ) $(OPTS) -o $@ 

obj/%.o: src/%.cc
	$(CXX) -c $^ $(OPTS) -o $@


.PHONY : clean
clean: 
	rm -f obj/* exec *.d /dev/shm/memcached* $(EXE) $(TEST_RUN) $(PERF_RUN) $(RPMA_RUN)
	make -C $(libralloc) clean
.PHONY : reset
reset:
	rm -f /dev/shm/memcached*

