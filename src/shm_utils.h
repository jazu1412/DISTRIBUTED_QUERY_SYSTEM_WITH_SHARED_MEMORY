#pragma once
#include <cstdint>
#include <cstddef>  // for size_t
#include <string>   // for std::string
#include <atomic>

#ifdef _WIN32
#include <Windows.h>
#endif

//  sysctl shmmax - command says 4mb but getting error when running shm with 4mb in mac
struct ShmCache {
    std::atomic<bool> ready;
    char query_id[64];
    size_t data_size;
    unsigned char data[1024*1024]; // doesnt breach 1mb  of data
};

void* openOrCreateShm(const std::string& name, size_t size);
