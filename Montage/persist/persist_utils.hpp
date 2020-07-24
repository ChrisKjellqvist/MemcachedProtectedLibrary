#ifndef PERSIST_UTILS_HPP
#define PERSIST_UTILS_HPP

#include "ConcurrentPrimitives.hpp"
#include<atomic>
#include<vector>

// A single-threaded circular buffer that grows exponentially
// when populated and never shrinks (for now).
// The buffer always allocates new spaces in chunks: the key
// idea is trying to allocate nodes in chunks to get
// better cache locality and lower the seeking overhead.
#define CIRCBUFFER_DEF_CAP 64
template<typename T>
class CircBuffer{
    struct Node{
        T payload;
        Node* next = nullptr;
    };
    // capacity
    int cap = CIRCBUFFER_DEF_CAP;
    // padding head/tail pointers: performs better when producer and consumer are on different threads.
    // head pointer, where things got popped off
    padded<Node*> head;
    // tail pointer, where things got pushed in
    padded<Node*> tail;
    // vector of allocated chunks. for destruction.
    std::vector<Node*> chunks;
    // find the next node of node p.
    Node* next_node(Node* p){
        // next pointer points to something non-trivial
        if (p->next){
            return p->next;
        } else {
            // next is null, go to the immediately next block.
            return ++p;
        }
    }
public:
    // constructor. allocate $cap Nodes and wrap the pointer around.
    CircBuffer(){
        head.ui = new Node[cap];
        head.ui[cap-1].next = head.ui;
        tail.ui = head.ui;
    }
    // destructor.
    ~CircBuffer(){
        for (auto itr = chunks.begin(); itr != chunks.end(); itr++){
            delete *itr;
        }
    }
    // push x into tail and move tail forward.
    void push(T x){
        // push x into tail
        tail.ui->payload = x;
        Node* next_tail = next_node(tail.ui);
        // if the buffer is full
        if(next_tail == head.ui){
            // double the capacity by allocating a new chunk
            // note that if we're in a chunk, it will be unfortunately broken into half.
            tail.ui->next = new Node[cap];
            chunks.push_back(tail.ui->next);
            // point the next pointer in newly created last block to the head
            tail.ui->next[cap-1].next = head.ui;
            // double the capacity
            cap <<= 1;
            // move the tail
            tail.ui = tail.ui->next;
        } else {
            tail.ui = next_tail;
        }
    }
    // try to pop the head into x.
    bool try_pop(T& x){
        if (head.ui == tail.ui){
            // empty.
            return false;
        } else {
            // not empty.
            x = head.ui->payload;
            head.ui = next_node(head.ui);
            return true;
        }
    }
    // try to pop the head before calling func.
    bool try_pop(void (*func)(T& x)){
        if (head.ui == tail.ui){
            // empty.
            return false;
        } else {
            // not empty.
            func(head.ui->payload);
            head.ui = next_node(head.ui);
            return true;
        }
    }
    void clear(){
        head.ui = tail.ui;
    }
}__attribute__((aligned(CACHELINE_SIZE)));

// a group of per-thread circular buffer
// NOTE: this is designed for single-consumer pattern only. The container is NOT thread safe.
template<typename T>
class PerThreadCircBuffer{
    // count of threads (and buffers)
    int count;
    padded<CircBuffer<T>*>* buffers;
public:
    PerThreadCircBuffer(int task_num){
        count = task_num;
        // init the buffers.
        buffers = new padded<CircBuffer<T>*>[count];
        for (int i = 0; i < count; i++){
            buffers[i].ui = new CircBuffer<T>();
        }
    }
    ~PerThreadCircBuffer(){
        for (int i = 0; i < count; i++){
            delete buffers[i].ui;
        }
        delete buffers;
    }
    void push(T x, int tid){
        buffers[tid].ui->push(x);
    }
    void pop_all(void (*func)(T& x)){
        // std::cout<<"pop_all called"<<std::endl;
        for (int i = 0; i < count; i++){
            while(buffers[i].ui->try_pop(func)){}
        }
    }
    void pop_all_local(void (*func)(T& x), int tid){
        while(buffers[tid].ui->try_pop(func)){}
    }
    bool try_pop_local(void (*func)(T& x), int tid){
        return buffers[tid].ui->try_pop(func);
    }
    void clear(){
        for (int i = 0; i < count; i++){
            buffers[i].ui->clear();
        }
    }
};

// a group of per-thread circular buffer
// NOTE: this is designed for single-consumer pattern only. The container is NOT thread safe.
template<typename T>
class PerThreadVector{
    // count of threads (and buffers)
    int count;
    padded<std::vector<T>*>* buffers;
public:
    PerThreadVector(int task_num){
        count = task_num;
        // init the buffers.
        buffers = new padded<std::vector<T>*>[count];
        for (int i = 0; i < count; i++){
            buffers[i].ui = new std::vector<T>();
        }
    }
    ~PerThreadVector(){
        for (int i = 0; i < count; i++){
            delete buffers[i].ui;
        }
        delete buffers;
    }
    void push(T x, int tid){
        buffers[tid].ui->push_back(x);
    }
    void pop_all(void (*func)(T& x)){
        for (int i = 0; i < count; i++){
            while(!buffers[i].ui->empty()){
                func(buffers[i].ui->back());
                buffers[i].ui->pop_back();
            }
        }
    }
    void pop_all_local(void (*func)(T& x), int tid){
        while(!buffers[tid].ui->empty()){
            func(buffers[tid].ui->back());
            buffers[tid].ui->pop_back();
        }
    }
    bool try_pop_local(void (*func)(T& x), int tid){
        if (buffers[tid].ui->empty()){
            return false;
        } else {
            func(buffers[tid].ui->back());
            buffers[tid].ui->pop_back();
            return true;
        }
        
    }
    void clear(){
        for (int i = 0; i < count; i++){
            buffers[i].ui->clear();
        }
    }
};

#endif