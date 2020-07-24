#ifndef GLOBALTESTCONFIG_HPP
#define GLOBALTESTCONFIG_HPP

#include <string.h>
#include <vector>
#include <map>
#include <unistd.h>
#include <iostream>
#include <sys/time.h>
#include <math.h>
#include <assert.h>
#include <malloc.h>
// #include <sys/time.h>
#include <chrono>
#include <sys/resource.h>
#include <hwloc.h>

#include "HarnessUtils.hpp"
#include "Rideable.hpp"
#include "Recorder.hpp"

class Test;
class Rideable;
class RideableFactory;

class GlobalTestConfig{
public:
	int task_num = 4;  // number of threads
	std::chrono::time_point<std::chrono::high_resolution_clock> start, finish; // timing structures
	double interval = 2.0;  // number of seconds to run test

	std::vector<hwloc_obj_t> affinities; // map from tid to CPU id
	hwloc_topology_t topology;
	
	int num_procs=24;
	Test* test=NULL;
	int testType=0;
	int rideableType=0;
	int verbose=0;
	unsigned long warmup = 3; // MBs of data to warm
	bool timeOut = true; // whether to abort on infinte loop
	std::string affinity;
	
	Recorder* recorder = NULL;
	std::vector<RideableFactory*> rideableFactories;
	std::vector<std::string> rideableNames;
	std::vector<Test*> tests;
	std::vector<std::string> testNames;
	std::string outFile;
	std::vector<Rideable*> allocatedRideables;

	long int total_operations=0;


	
	GlobalTestConfig();
	~GlobalTestConfig();

	// allocate test object at runtime.
	Test* allocTest();


	// for configuration set up
	void parseCommandLine(int argc, char** argv);
	std::string getTestName();
	void addTestOption(Test* t, const char name[]);

	// for accessing environment and args
	void setEnv(std::string,std::string);
	bool checkEnv(std::string);
	std::string getEnv(std::string);

	void setArg(std::string,void*);
	bool checkArg(std::string);
	void* getArg(std::string);
	
	
	// Run the test
	void runTest();

private:
	// TODO: add affinity builders.
	// // Affinity functions
	// // a bunch needed because of recursive traversal of topologies.
	void buildAffinity();
	void buildDFSAffinity_helper(hwloc_obj_t obj);
	void buildDFSAffinity();
	int buildDefaultAffinity_findCoresInSocket(hwloc_obj_t obj, std::vector<hwloc_obj_t>* cores);
	int buildDefaultAffinity_buildPUsInCores(std::vector<hwloc_obj_t>* cores);
	int buildDefaultAffinity_findAndBuildSockets(hwloc_obj_t obj);
	void buildDefaultAffinity();
	void buildSingleAffinity_helper(hwloc_obj_t obj);
	void buildSingleAffinity();

	std::map<std::string,std::string> environment;
	std::map<std::string,void*> arguments;
	
	void createTest();
	char* argv0;
	void printargdef();

};

// local test configuration, one per thread
class LocalTestConfig{
public:
	int tid;
	unsigned int seed;
	unsigned cpu;
	hwloc_cpuset_t cpuset;
};

class CombinedTestConfig{
public:
	GlobalTestConfig* gtc;
	LocalTestConfig* ltc;
};

class Test{
public:
	// called by one (master) thread
	virtual void init(GlobalTestConfig* gtc)=0;
	virtual void cleanup(GlobalTestConfig* gtc)=0;

	// called by all threads in parallel
	virtual void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){}
	// runs the test
	// returns number of operations completed by that thread
	virtual int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc)=0;
};


#endif