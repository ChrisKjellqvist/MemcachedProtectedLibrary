# Memcached

CHRIS NOTES
**IMPORANT***


src/pfence_util.h -- macros for fences
src/rpmalloc.hpp -- main header file
src/pptr.hpp -- header containing persistent pointer class
RegionManager.hpp:lookup -- find base pointers for regions
	size is stored 

Make sure to export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd)/lib


QUESTIONS:
Arguments for sfence?
mmap from client
make client seperate from server

Memcached is a high performance multithreaded event-based key/value cache
store intended to be used in a distributed system.

See: https://memcached.org/about

A fun story explaining usage: https://memcached.org/tutorial

If you're having trouble, try the wiki: https://memcached.org/wiki

If you're trying to troubleshoot odd behavior or timeouts, see:
https://memcached.org/timeouts

https://memcached.org/ is a good resource in general. Please use the mailing
list to ask questions, github issues aren't seen by everyone!

## Dependencies

* libevent, http://www.monkey.org/~provos/libevent/ (libevent-dev)
* libseccomp, (optional, experimental, linux) - enables process restrictions for
  better security. Tested only on x86_64 architectures.

## Environment

Be warned that the -k (mlockall) option to memcached might be
dangerous when using a large cache.  Just make sure the memcached machines
don't swap.  memcached does non-blocking network I/O, but not disk.  (it
should never go to disk, or you've lost the whole point of it)

## Website

* http://www.memcached.org

## Contributing

See https://github.com/memcached/memcached/wiki/DevelopmentRepos
