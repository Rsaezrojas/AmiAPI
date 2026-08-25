#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include "internal.h"

enum AmiAPIError amiapi_transport_connect(struct AmiAPITransport *transport,
                                          const char *host,
                                          unsigned short port)
{
    struct addrinfo hints;
    struct addrinfo *result;
    struct addrinfo *it;
    char service[16];
    int rc;

    if (transport == NULL || host == NULL)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    transport->fd = -1;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(service, sizeof(service), "%u", (unsigned int)port);
    rc = getaddrinfo(host, service, &hints, &result);
    if (rc != 0)
        return AMIAPI_ERR_DNS;

    for (it = result; it != NULL; it = it->ai_next) {
        transport->fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (transport->fd < 0)
            continue;

        if (connect(transport->fd, it->ai_addr, it->ai_addrlen) == 0)
            break;

        close(transport->fd);
        transport->fd = -1;
    }

    freeaddrinfo(result);

    if (transport->fd < 0)
        return AMIAPI_ERR_CONNECT;

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
        ssize_t n = send(transport->fd, p + done, size - done, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return AMIAPI_ERR_SEND;
        }
        if (n == 0)
            return AMIAPI_ERR_SEND;
        done += (size_t)n;
    }

    return AMIAPI_OK;
}

long amiapi_transport_receive(struct AmiAPITransport *transport,
                              void *buffer,
                              size_t size)
{
    ssize_t n;

    if (transport == NULL || transport->fd < 0 || buffer == NULL || size == 0)
        return -1;

    do {
        n = recv(transport->fd, buffer, size, 0);
    } while (n < 0 && errno == EINTR);

    return (long)n;
}

void amiapi_transport_close(struct AmiAPITransport *transport)
{
    if (transport != NULL && transport->fd >= 0) {
        close(transport->fd);
        transport->fd = -1;
    }
}

void amiapi_transport_platform_cleanup(void)
{
}

enum AmiAPIError amiapi_transport_prepare_tls(void)
{
    return AMIAPI_ERR_CONNECT;
}

enum AmiAPIError amiapi_transport_start_tls(
    struct AmiAPITransport *transport,
    const char *host)
{
    (void)transport;
    (void)host;
    return AMIAPI_ERR_CONNECT;
}
