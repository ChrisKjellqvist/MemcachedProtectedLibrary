#ifndef PERSISTENT_HPP
#define PERSISTENT_HPP

#include <AllocatorMacro.hpp>
#include <ralloc.hpp>

using namespace std;

class Persistent {
public:
	void* operator new(size_t size){
		// cout<<"persistent allocator called."<<endl;
		// void* ret = malloc(size);
		void* ret = RP_malloc(size);
		if (!ret){
			cerr << "Persistent::new failed: no free memory" << endl;
			exit(1);
		}
		return ret;
	}

	void* operator new (std::size_t size, void* ptr) {
		return ptr;
	}

	void operator delete(void * p) { 
		RP_free(p); 
	} 

	static void init(){
		std::cout << "initializing memcacheD" << std::endl;
		// memcached_init();
	}

	static void finalize(){
		// pm_close();
		//memcached_close();
		std::cout << "closing memcacheD" << std::endl;
	}
	static InuseRecovery::iterator recover(){
		return RP_recover();
	}
};

#endif