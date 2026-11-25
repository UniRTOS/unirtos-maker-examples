#include "xiaozhi.h"
#include "xiaozhi_config.h"
#include "qosa_datacall.h"
static _Atomic int activating;
static qosa_datacall_conn_t connection(void)
{
    return qosa_datacall_conn_new(XIAOZHI_SIM_ID, XIAOZHI_PDP_ID, QOSA_DATACALL_CONN_TCPIP);
}
int xz_net_ready(void)
{
    qosa_datacall_conn_t c = connection();
    return c != QOSA_DATACALL_CONN_INVALID && qosa_datacall_get_status(c);
}
static void activated(void *arg, void *info)
{
    (void)arg; (void)info;
    activating = 0; /* Read actual PDP status on next application poll. */
}
void xz_net_request(void)
{
    qosa_datacall_conn_t c;
    if (xz_net_ready() || activating || !qosa_datacall_wait_attached(XIAOZHI_SIM_ID, 0)) return;
    c = connection();
    if (c == QOSA_DATACALL_CONN_INVALID) return;
    activating = 1; /* Set before a potentially synchronous callback. */
    if (qosa_datacall_start_async(c, 30, activated, QOSA_NULL) != QOSA_DATACALL_OK) activating = 0;
}