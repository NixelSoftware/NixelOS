#include "tcp.h"
#include "../../user/sys_api.h"
#include "../../../kernel/net/tcp.h"
int nixel_tls_tcp_send(void *context, unsigned char *data, int size) {
    uint32_t handle;
    if (!context || !data || size <= 0) return -1;
    handle = *(uint32_t *)context;
    int state = sys_tcp_state(handle);
    if (state != TCP_ESTABLISHED && state != TCP_CLOSE_WAIT) return -1;
    if (size > TCP_MAX_PAYLOAD) size = TCP_MAX_PAYLOAD;
    return sys_tcp_send(handle, data, size) == 1 ? size : 0;
}
int nixel_tls_tcp_receive(void *context, unsigned char *data, int size) {
    uint32_t handle;
    if (!context || !data || size <= 0) return -1;
    handle = *(uint32_t *)context;
    int n = sys_tcp_read(handle, data, size > 4096 ? 4096 : size);
    int state;
    if (n) return n;
    state = sys_tcp_state(handle);
    if (state == TCP_ESTABLISHED) return 0;
    return state == TCP_CLOSE_WAIT || state == TCP_CLOSED ? -2 : -1;
}
