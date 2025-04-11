#include "shm_utils.h"
#include <iostream>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

void* openOrCreateShm(const std::string& name, size_t size){
    std::string realName = "/" + name;
    int fd = shm_open(realName.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd<0) {
        perror("shm_open");
        return nullptr;
    }
    ftruncate(fd, size);
    void* p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (p==MAP_FAILED) {
        perror("mmap");
        return nullptr;
    }
    ShmCache* sc = (ShmCache*) p;
    sc->ready = false;
    sc->data_size=0;
    memset(sc->query_id, 0, sizeof(sc->query_id));
    return p;
}
