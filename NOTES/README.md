# Memcached

CHRIS NOTES
**IMPORANT
***
Currently not linked -static ally because it causes weird problems with redefinitions with
memcached's required libraries. When we get Wentao's allocator it'll be easier to statically
link that in I think. Either way, we can't protect allocator state right now :(

src/pfence_util.h -- macros for fences
src/rpmalloc.hpp -- main header file
src/pptr.hpp -- header containing persistent pointer class
RegionManager.hpp:lookup -- find base pointers for regions
	size is stored 



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
