#include "xallocator.h"
#include <stdio.h>
#include "xlnk-ioctl.h"
#include "xlnk-os.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <map>
#include <mutex>

#define XLNK_DRIVER_PATH "/dev/xlnk"
 
static int xlnkFileFd = -1;
struct BOData {
    unsigned int boHandle;
    uint64_t paddr;
    size_t size;
};

std::mutex mBOMapLock;
std::map<uint64_t, BOData> mBoMap; 

static int xlnkFileHandle(void)
{
    if(xlnkFileFd < 0) {
        xlnkFileFd = open(XLNK_DRIVER_PATH, O_RDWR);
        if(xlnkFileFd < 0) {
            fprintf(stderr, "Failed to open /dev/xlnk\n");
            exit(-1);
        }
    }
    return xlnkFileFd;
}

void* xallocate(size_t len) {
    unsigned long long pAddr;
    void* vAddr;
    int status;
    unsigned int bufId;
    int linkId;

    xlnk_args xlnkArgs;
    xlnkArgs.allocbuf.len = len;
    xlnkArgs.allocbuf.cacheable = 0;//cacheable;
    status = ioctl(xlnkFileHandle(), XLNK_IOCALLOCBUF, &xlnkArgs);
    linkId = xlnkArgs.allocbuf.id;
    pAddr = (unsigned long long) xlnkArgs.allocbuf.phyaddr;
    printf("%s:%d paddr:%llx \n",__func__,__LINE__,pAddr);
    if (status) {
        printf("allocbuf ioctl returned:%s", status);
        return NULL;
    }
    if (linkId <= 0) {
        printf("buf ID = 0");
        return NULL;
    }
    vAddr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, xlnkFileHandle(), linkId << 16);
    if (vAddr == (void *)-1) {
        printf(" failed");
        return NULL;
    }
    BOData D;
    D.boHandle = linkId;
    D.paddr = pAddr;
    D.size = len;
    mBoMap.insert(std::pair<uint64_t, BOData>(reinterpret_cast<uint64_t>(vAddr), D));
    //printf("%s:%d vaddr:%llx \n",__func__,__LINE__,reinterpret_cast<uint64_t>(vAddr));
    return vAddr;
}

void xdeallocate(void *buf) {
    xlnk_args xlnkArgs;
    auto it = mBoMap.find(reinterpret_cast<uint64_t>(buf));
    if (it == mBoMap.end()) {
        printf("xclAllocUserPtrBO Bad Host PTR\n");
        return;
    }
    xlnkArgs.freebuf.id = it->second.boHandle;
    xlnkArgs.freebuf.buf = (xlnk_intptr_type)buf;

    munmap(buf, it->second.size);
    ioctl(xlnkFileHandle(), XLNK_IOCFREEBUF, &xlnkArgs);
}
