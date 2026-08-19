#include "plgldr.h"

#include <string.h>

static Handle s_port;
static int    s_refs;

Result hs_plgldr_init(void)
{
    if (s_refs++ > 0) return 0;

    Result rc = svcConnectToPort(&s_port, "plg:ldr");
    if (R_FAILED(rc)) {
        s_port = 0;
        s_refs = 0;
    }
    return rc;
}

void hs_plgldr_exit(void)
{
    if (s_refs == 0) return;
    if (--s_refs > 0) return;

    if (s_port) svcCloseHandle(s_port);
    s_port = 0;
}

Result hs_plgldr_is_enabled(bool *out_enabled)
{
    if (!s_port || !out_enabled) return -1;

    u32 *cmd = getThreadCommandBuffer();
    cmd[0] = IPC_MakeHeader(2, 0, 0);

    Result rc = svcSendSyncRequest(s_port);
    if (R_FAILED(rc)) return rc;

    *out_enabled = cmd[2] != 0;
    return (Result)cmd[1];
}

Result hs_plgldr_set_enabled(bool enabled)
{
    if (!s_port) return -1;

    u32 *cmd = getThreadCommandBuffer();
    cmd[0] = IPC_MakeHeader(3, 1, 0);
    cmd[1] = (u32)enabled;

    Result rc = svcSendSyncRequest(s_port);
    if (R_FAILED(rc)) return rc;

    return (Result)cmd[1];
}

Result hs_plgldr_set_load_params(const hs_plg_params_t *p)
{
    if (!s_port || !p) return -1;

    // The service reads both buffers straight out of this process, so they have
    // to stay put for the duration of the call - hence local copies rather than
    // pointers into the caller's struct, which is const and may live anywhere.
    char path[256];
    u32  config[32];
    memcpy(path, p->path, sizeof(path));
    memcpy(config, p->config, sizeof(config));

    u32 *cmd = getThreadCommandBuffer();
    cmd[0] = IPC_MakeHeader(4, 2, 4);

    // Three flag bytes share one word. Older builds of the service read only
    // the low byte; packing the other two as zero is therefore identical on
    // both, which is why the defaults matter more than they look.
    cmd[1] = (u32)p->no_flash
           | ((u32)p->memory_strategy << 8)
           | ((u32)p->persistent << 16);
    cmd[2] = p->low_title_id;
    cmd[3] = IPC_Desc_Buffer(sizeof(path), IPC_BUFFER_R);
    cmd[4] = (u32)path;
    cmd[5] = IPC_Desc_Buffer(sizeof(config), IPC_BUFFER_R);
    cmd[6] = (u32)config;

    Result rc = svcSendSyncRequest(s_port);
    if (R_FAILED(rc)) return rc;

    return (Result)cmd[1];
}
