#include "shm_utils.h"
#include <iostream>
#include <cstring>
#ifdef __APPLE__
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#endif

void* openOrCreateShm(const std::string& name, size_t size){
#ifdef _WIN32
    HANDLE hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL,
                        PAGE_READWRITE, 0, (DWORD)size, name.c_str());
    if (!hMapFile) {
        std::cerr << "CreateFileMapping failed." << std::endl;
        return nullptr;
    }
    void* p = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!p) {
        std::cerr << "MapViewOfFile failed." << std::endl;
    }
    ShmCache* sc = (ShmCache*) p;
    sc->ready = false;
    sc->data_size = 0;
    memset(sc->query_id, 0, sizeof(sc->query_id));
    return p;
#else
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
#endif
}
