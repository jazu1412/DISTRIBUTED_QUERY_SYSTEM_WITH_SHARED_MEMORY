#pragma once
#include <cstdint>
#include <cstddef>  
#include <string>   
#include <atomic>


//  sysctl shmmax - command returns 4mb but getting error when running shm with 4mb in mac so changed as 1mb
struct ShmCache {
    std::atomic<bool> ready;
    char query_id[64];
    size_t data_size;
    unsigned char data[1024*1024]; //1mb
};

void* openOrCreateShm(const std::string& name, size_t size);
