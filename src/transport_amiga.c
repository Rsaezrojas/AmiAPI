#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <sys/types.h>
static struct Library *AmiAPISocketBase = NULL;
#define BSDSOCKET_BASE_NAME AmiAPISocketBase
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include "internal.h"

static enum AmiAPIError open_socket_library(void)
{
    if (AmiAPISocketBase) return AMIAPI_OK;
    AmiAPISocketBase = OpenLibrary("bsdsocket.library", 4);
    return AmiAPISocketBase ? AMIAPI_OK : AMIAPI_ERR_SOCKET_LIBRARY;
}

enum AmiAPIError amiapi_transport_connect(struct AmiAPITransport *t, const char *host, unsigned short port)
{
    struct hostent *h; struct sockaddr_in a; enum AmiAPIError e; LONG fd;
    if (!t || !host) return AMIAPI_ERR_INVALID_ARGUMENT;
    t->fd = -1; e = open_socket_library(); if (e != AMIAPI_OK) return e;
    h = gethostbyname((STRPTR)host);
    if (!h || !h->h_addr_list || !h->h_addr_list[0]) return AMIAPI_ERR_DNS;
    memset(&a, 0, sizeof(a)); a.sin_family = AF_INET; a.sin_port = htons(port);
    memcpy(&a.sin_addr, h->h_addr_list[0], sizeof(a.sin_addr));
    fd = socket(AF_INET, SOCK_STREAM, 0); if (fd < 0) return AMIAPI_ERR_CONNECT;
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { CloseSocket(fd); return AMIAPI_ERR_CONNECT; }
    t->fd = (int)fd; return AMIAPI_OK;
}

enum AmiAPIError amiapi_transport_send_all(struct AmiAPITransport *t, const void *data, size_t size)
{
    const unsigned char *p; size_t done = 0;
    if (!t || t->fd < 0 || !data) return AMIAPI_ERR_INVALID_ARGUMENT;
    p = (const unsigned char *)data;
    while (done < size) {
        size_t rem = size - done; LONG n;
        if (rem > 0x7fffffffUL) rem = 0x7fffffffUL;
        n = send(t->fd, (char *)(p + done), (LONG)rem, 0);
        if (n <= 0) return AMIAPI_ERR_SEND;
        done += (size_t)n;
    }
    return AMIAPI_OK;
}
long amiapi_transport_receive(struct AmiAPITransport *t, void *buf, size_t size)
{
    size_t req; if (!t || t->fd < 0 || !buf || !size) return -1;
    req = size; if (req > 0x7fffffffUL) req = 0x7fffffffUL;
    return (long)recv(t->fd, (char *)buf, (LONG)req, 0);
}
void amiapi_transport_close(struct AmiAPITransport *t)
{ if (t && t->fd >= 0) { CloseSocket(t->fd); t->fd = -1; } }
void amiapi_transport_platform_cleanup(void)
{ if (AmiAPISocketBase) { CloseLibrary(AmiAPISocketBase); AmiAPISocketBase = NULL; } }
