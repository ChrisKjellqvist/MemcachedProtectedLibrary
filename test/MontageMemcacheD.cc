#include <iostream>
#include <string>
#include <thread>
#include <random>
#include <time.h>
#include <sys/time.h>

#include "montage_memcached.h"
#include "memcacheDTest.hpp"

#include "ConcurrentPrimitives.hpp"
#include "HarnessUtils.hpp"
#include "CustomTypes.hpp"

using namespace std;

int main(int argc, char *argv[])
{
	GlobalTestConfig gtc;
	gtc.addTestOption(new MemcacheDTest(), "MemcacheDTest");
	gtc.parseCommandLine(argc, argv);
	
	gtc.runTest();

	// print out results
	if(gtc.verbose){
		printf("Operations/sec: %ld\n",(uint64_t)(gtc.total_operations/gtc.interval));
	}
	else{
		printf("%d,%ld,%s\n",gtc.task_num,(uint64_t)(gtc.total_operations/gtc.interval),gtc.getTestName().c_str());
	}
}