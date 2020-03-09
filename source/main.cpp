#include "common.hpp"
#include "fs/IFileSystem.hpp"
#include "ftp/ftp.hpp"
#include "util/logger.hpp"
#include "util/time.hpp"

#include <switch.h>

extern "C" {

extern u32 __start__;

u32 __nx_applet_type = AppletType_None;

#define INNER_HEAP_SIZE 0xA7000
size_t nx_inner_heap_size = INNER_HEAP_SIZE;
char nx_inner_heap[INNER_HEAP_SIZE];

void __libnx_initheap(void);
void __appInit(void);
void __appExit(void);
}

void __libnx_initheap(void) {
    void *addr = nx_inner_heap;
    size_t size = nx_inner_heap_size;

    /* Newlib */
    extern char *fake_heap_start;
    extern char *fake_heap_end;

    fake_heap_start = (char *)addr;
    fake_heap_end = (char *)addr + size;
}

constexpr SocketInitConfig socketInitConfig = {
    .bsdsockets_version = 1,

    .tcp_tx_buf_size = 0x800,
    .tcp_rx_buf_size = 0x800,
    .tcp_tx_buf_max_size = 0x25000,
    .tcp_rx_buf_max_size = 0x25000,

    //We don't use UDP, set all UDP buffers to 0
    .udp_tx_buf_size = 0x0,
    .udp_rx_buf_size = 0x0,

    .sb_efficiency = 1,
};

void __appInit(void) {
    /* Sleep for 5 seconds to not interfere with anything else. */
    //svcSleepThread(5e+9);

    /* Crash on service init failure. */
    sm::DoWithSmSession([] {
        /* Set HOS version. */
        R_ASSERT(setsysInitialize());
        SetSysFirmwareVersion fw;
        R_ASSERT(setsysGetFirmwareVersion(&fw));
        hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();

        R_ASSERT(appletInitialize());
        R_ASSERT(timeInitialize());
        R_ASSERT(fsInitialize());

        R_ASSERT(hidInitialize());

        R_ASSERT(socketInitialize(&socketInitConfig));
    });
}

void __appExit(void) {
    socketExit();

    hidExit();

    fsExit();
    timeExit();
    appletExit();
}

LoopStatus loop(FTP &ftp) {
    LoopStatus status = LoopStatus::CONTINUE;

    while (appletMainLoop()) {
        svcSleepThread(1e+7);
        status = ftp.Loop();
        if (status != LoopStatus::CONTINUE)
            return status;
    }
    return LoopStatus::EXIT;
}

int main(int argc, char **argv) {
    int nxlinkSocket = nxlinkStdio();

    FsFileSystem sdmcFs;
    R_ASSERT(fsOpenSdCardFileSystem(&sdmcFs));
    auto fs = std::make_shared<IFileSystem>(std::move(sdmcFs));
    InitializeLog(fs);
    R_LOG(hos::time::Initialize());

    LOG_DEBUG("Start");

    {
        FTP ftp(fs);

        LoopStatus status = LoopStatus::RESTART;
        while (status == LoopStatus::RESTART && appletMainLoop()) {
            if (R_FAILED(ftp.Init()))
                break;

            status = loop(ftp);

            ftp.Exit();
        }
    }

    LOG_DEBUG("End");

    close(nxlinkSocket);
    return 0;
}
