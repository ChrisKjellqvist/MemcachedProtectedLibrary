CC=clang
CCOPTS = -Ofast
OBJECTS = plib.o

all: plib.o

%.o: %.cc
	$(CC) -c $(CCOPTS) $^ -o $@

clean:
	rm -rf plib.o

