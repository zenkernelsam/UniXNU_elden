/* SPDX-License-Identifier: MIT */
#include "CallGate.h"
#include <atomic>
#include <thread>
#include <vector>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x))std::abort();}while(0)
int main(){
    EldenGpuCallGate gate;CHECK(eldenGpuEmpty(&gate));
    {EldenGpuCallLease a(&gate);CHECK(a);eldenGpuClose(&gate);CHECK(!eldenGpuEmpty(&gate));EldenGpuCallLease b(&gate);CHECK(!b);}
    CHECK(eldenGpuEmpty(&gate));
    gate={};gate.active=UINT32_MAX;CHECK(!eldenGpuEnter(&gate));CHECK(gate.active==UINT32_MAX);
    unsigned visits=0;
    for(unsigned run=0;run<32;++run){
        EldenGpuCallGate g;std::atomic<bool> enter{false},release{false};std::atomic<unsigned> inside{0};std::vector<std::thread> threads;
        for(unsigned i=0;i<16;++i)threads.emplace_back([&]{while(!enter.load())std::this_thread::yield();EldenGpuCallLease lease(&g);CHECK(lease);++inside;while(!release.load())std::this_thread::yield();});
        enter=true;while(inside.load()!=16)std::this_thread::yield();eldenGpuClose(&g);CHECK(!eldenGpuEmpty(&g));
        for(unsigned i=0;i<100;++i)CHECK(!eldenGpuEnter(&g));release=true;for(auto &t:threads)t.join();CHECK(eldenGpuEmpty(&g));visits+=16;
    }
    std::printf("PASS: GPU close gate, overflow refusal, %u held concurrent calls and3200 denied entries\n",visits);
}
