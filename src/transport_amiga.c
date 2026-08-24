/*
 * AmiAPI AmigaOS TCP transport.
 *
 * This backend targets AmigaOS 3.x networking stacks that provide the
 * bsdsocket.library API (Roadshow, Miami/MiamiDx, Genesis/AmiTCP, etc.).
 *
 * The SocketBase symbol is deliberately private to this translation unit.
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>

#include <sys/types.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#include <stdio.h>
#include <string.h>

#include "internal.h"

struct Library *SocketBase = NULL;

static enum AmiAPIError open_socket_library(void)
{
    if (SocketBase != NULL)
        return AMIAPI_OK;

    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (SocketBase == NULL)
        return AMIAPI_ERR_SOCKET_LIBRARY;

    return AMIAPI_OK;
}

enum AmiAPIError amiapi_transport_connect(struct AmiAPITransport *transport,
                                          const char *host,
                                          unsigned short port)
{
    struct hostent *host_entry;
    struct sockaddr_in address;
    enum AmiAPIError err;
    LONG fd;

    if (transport == NULL || host == NULL)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    transport->fd = -1;

    err = open_socket_library();
    if (err != AMIAPI_OK)
        return err;

    host_entry = gethostbyname((STRPTR)host);
    if (host_entry == NULL || host_entry->h_addr_list == NULL ||
        host_entry->h_addr_list[0] == NULL)
        return AMIAPI_ERR_DNS;

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    memcpy(&address.sin_addr, host_entry->h_addr_list[0],
           sizeof(address.sin_addr));

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return AMIAPI_ERR_CONNECT;

    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        CloseSocket(fd);
        return AMIAPI_ERR_CONNECT;
    }

    transport->fd = (int)fd;
    return AMIAPI_OK;
}

enum AmiAPIError amiapi_transport_send_all(struct AmiAPITransport *transport,
                                           const void *data,
                                           size_t size)
{
    const unsigned char *p;
    size_t done;

    if (transport == NULL || transport->fd < 0 || data == NULL)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    p = (const unsigned char *)data;
    done = 0;

    while (done < size) {
        LONG amount;
        size_t remaining = size - done;

        /*
         * LONG is the native bsdsocket length/result type on classic AmigaOS.
         * HTTP request headers are tiny, but cap defensively anyway.
         */
        if (remaining > 0x7fffffffUL)
            remaining = 0x7fffffffUL;

        amount = send(transport->fd, (char *)(p + done),
                      (LONG)remaining, 0);
        if (amount <= 0)
            return AMIAPI_ERR_SEND;

        done += (size_t)amount;
    }

    return AMIAPI_OK;
}

long amiapi_transport_receive(struct AmiAPITransport *transport,
                              void *buffer,
                              size_t size)
{
    LONG amount;
    size_t requested;

    if (transport == NULL || transport->fd < 0 ||
        buffer == NULL || size == 0)
        return -1;

    requested = size;
    if (requested > 0x7fffffffUL)
        requested = 0x7fffffffUL;

    amount = recv(transport->fd, (char *)buffer, (LONG)requested, 0);
    return (long)amount;
}

void amiapi_transport_close(struct AmiAPITransport *transport)
{
    if (transport != NULL && transport->fd >= 0) {
        CloseSocket(transport->fd);
        transport->fd = -1;
    }
}

void amiapi_transport_platform_cleanup(void)
{
    if (SocketBase != NULL) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}
