#include "ftp.h"

#include <switch.h>

#define HEAP_SIZE 0xA7000

// we aren't an applet
u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

// setup a fake heap
char fake_heap[HEAP_SIZE];

// we override libnx internals to do a minimal init
void __libnx_initheap(void)
{
    extern char* fake_heap_start;
    extern char* fake_heap_end;

    // setup newlib fake heap
    fake_heap_start = fake_heap;
    fake_heap_end = fake_heap + HEAP_SIZE;
}

#define R_ASSERT(res_expr)      \
    ({                          \
        Result rc = (res_expr); \
        if (R_FAILED(rc))       \
            fatalThrow(rc);     \
    })

void __appInit(void)
{
    R_ASSERT(smInitialize());
    R_ASSERT(setsysInitialize());
    SetSysFirmwareVersion fw;
    if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
        hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
    setsysExit();
    R_ASSERT(fsInitialize());
    R_ASSERT(fsdevMountSdmc());
    R_ASSERT(timeInitialize());
    R_ASSERT(hidsysInitialize());

    static const SocketInitConfig socketInitConfig = {
        .bsdsockets_version = 1,

        .tcp_tx_buf_size = 0x800,
        .tcp_rx_buf_size = 0x800,
        .tcp_tx_buf_max_size = 0x25000,
        .tcp_rx_buf_max_size = 0x25000,

        //We don't use UDP, set all UDP buffers to 0
        .udp_tx_buf_size = 0,
        .udp_rx_buf_size = 0,

        .sb_efficiency = 1,
    };
    R_ASSERT(socketInitialize(&socketInitConfig));
    smExit();
}

void __appExit(void)
{
    socketExit();
    hidsysExit();
    timeExit();
    fsdevUnmountAll();
    fsExit();
}

static loop_status_t loop(loop_status_t (*callback)(void))
{
    loop_status_t status = LOOP_CONTINUE;

    while (true)
    {
        svcSleepThread(1e+7);
        status = callback();
        if (status != LOOP_CONTINUE)
            return status;
    }
    return LOOP_EXIT;
}

int main()
{
    loop_status_t status = LOOP_RESTART;

    ftp_pre_init();
    while (status == LOOP_RESTART)
    {
        /* initialize ftp subsystem */
        if (ftp_init() == 0)
        {
            /* ftp loop */
            status = loop(ftp_loop);

            /* done with ftp */
            ftp_exit();
        }
        else
            status = LOOP_EXIT;
    }
    ftp_post_exit();

    return 0;
}
