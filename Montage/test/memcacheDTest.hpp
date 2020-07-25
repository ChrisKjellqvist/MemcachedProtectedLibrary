#ifndef YCSB_HPP
#define YCSB_HPP

#include "TestConfig.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <thread>
#include <iostream>
#include <limits>
#include <cstdlib>
#include <random>
#include <pku_memcached.h>
#include "Persistent.hpp"
#include "persist_struct_api.hpp"
#include "montage_memcached.h"

using namespace std;
class MemcacheDTest : public Test{
public:
    vector<std::string>** traces;
    std::string value_buffer;
    size_t val_size = 32;
    std::string thd_num;
    MemcacheDTest(){}

    void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        pds::init_thread(ltc->tid);
    }
    void init(GlobalTestConfig* gtc){
        // init Persistent allocator
        std::cout << std::endl;
        //Persistent::init();
        memcached_init();

        // init epoch system
        std::cout << std::endl;
        pds::init(gtc);

        value_buffer.reserve(val_size);
        value_buffer.clear();
        std::mt19937_64 gen_v(7);
        for (size_t i = 0; i < val_size - 1; i++) {
            value_buffer += (char)((i % 2 == 0 ? 'A' : 'a') + (gen_v() % 26));
        }
        value_buffer += '\0';

        thd_num = to_string(gtc->task_num);

        traces = new vector<std::string>* [gtc->task_num];
        for(int i=0;i<gtc->task_num;i++){
            traces[i] = new vector<std::string>();
            std::ifstream infile("data");
            std::string cmd;
            while(getline(infile, cmd)){
                traces[i]->push_back(cmd);
            }
        }
        /* set interval to inf so this won't be killed by timeout */
        gtc->interval = numeric_limits<double>::max();
    }
    void operation(const std::string& t, int tid, bool rm = false){

        char nbuff[64];
        char qbuff[64];
        size_t len;
        uint32_t flags;
        memcached_return_t err;
        
        strcpy(qbuff, value_buffer.c_str());

        string tag = t.substr(0, 3);
        if (tag == "Add" && rm){
            strcpy(nbuff, t.substr(4).c_str());
            auto ret = montage_put(nbuff, strlen(nbuff), qbuff, strlen(qbuff), 0, 0);
            if(ret == MEMCACHED_SUCCESS){
                std::cout << "success" << std::endl;
            }else{
                std::cout << "fails" << std::endl;
            }
        } else{// Update
            strcpy(nbuff, t.substr(4).c_str());
            char* k = montage_get(nbuff, strlen(nbuff), &len, &flags, &err);
            if( err == MEMCACHED_SUCCESS){
                std::cout << k << std::endl;
            }else{
                std::cout << "fails" << std::endl;
            }
        }
    }

    int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
        int tid = ltc->tid;
        int ops = 0;
        std::mt19937_64 gen_v(ltc->tid);
        
        for (size_t i = 0; i < traces[tid]->size(); i++) {
            operation(traces[tid]->at(i), tid, gen_v()&true);
            ops++;
        }
        return ops;
    }
    void cleanup(GlobalTestConfig* gtc){
        pds::finalize();
        //Persistent::finalize();
        memcached_close();
        for(int i=0;i<gtc->task_num;i++){
            delete traces[i];
        }
        delete traces;
    }
};

#endif