#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <netdb.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include "internal.h"

static int set_nonblocking(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return 0;

    if ((flags & O_NONBLOCK) != 0)
        return 1;

    return fcntl(
        fd,
        F_SETFL,
        flags | O_NONBLOCK
    ) == 0;
}

static int wait_for_socket(
    int fd,
    int want_read,
    unsigned int timeout_seconds)
{
    struct pollfd pfd;
    int timeout_ms;
    int rc;

    if (fd < 0)
        return 0;

    pfd.fd = fd;
    pfd.events = want_read ? POLLIN : POLLOUT;
    pfd.revents = 0;

    if (timeout_seconds == 0) {
        timeout_ms = -1;
    }
    else if (timeout_seconds >
             (unsigned int)(INT_MAX / 1000)) {
        timeout_ms = INT_MAX;
    }
    else {
        timeout_ms = (int)timeout_seconds * 1000;
    }

    do {
        rc = poll(&pfd, 1, timeout_ms);
    } while (rc < 0 && errno == EINTR);

    if (rc <= 0)
        return 0;

    if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
        return 0;

    return (pfd.revents & pfd.events) != 0;
}

static int wait_for_ssl(
    struct AmiAPITransport *transport,
    int ssl_error)
{
    if (!transport)
        return 0;

    if (ssl_error == SSL_ERROR_WANT_READ) {
        return wait_for_socket(
            transport->fd,
            1,
            transport->timeout_seconds
        );
    }

    if (ssl_error == SSL_ERROR_WANT_WRITE) {
        return wait_for_socket(
            transport->fd,
            0,
            transport->timeout_seconds
        );
    }

    return 0;
}

static enum AmiAPIError apply_socket_timeouts(
    struct AmiAPITransport *transport)
{
    struct timeval timeout;

    if (!transport || transport->fd < 0)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    if (transport->timeout_seconds == 0)
        return AMIAPI_OK;

    timeout.tv_sec = (long)transport->timeout_seconds;
    timeout.tv_usec = 0;

    if (setsockopt(
            transport->fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            sizeof(timeout)) != 0)
    {
        return AMIAPI_ERR_CONNECT;
    }

    if (setsockopt(
            transport->fd,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &timeout,
            sizeof(timeout)) != 0)
    {
        return AMIAPI_ERR_CONNECT;
    }

    return AMIAPI_OK;
}

enum AmiAPIError amiapi_transport_prepare_tls(void)
{
    /*
     * OpenSSL 1.1.0 and later initialize themselves automatically.
     * OPENSSL_init_ssl() is still useful here because it gives us an
     * explicit failure point for TLS initialization.
     */
    if (OPENSSL_init_ssl(0, NULL) != 1)
        return AMIAPI_ERR_CONNECT;

    return AMIAPI_OK;
}

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
    transport->tls_ctx = NULL;
    transport->tls_ssl = NULL;
    transport->use_tls = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(service, sizeof(service), "%u", (unsigned int)port);

    rc = getaddrinfo(host, service, &hints, &result);
    if (rc != 0)
        return AMIAPI_ERR_DNS;

    for (it = result; it != NULL; it = it->ai_next) {
        transport->fd = socket(
            it->ai_family,
            it->ai_socktype,
            it->ai_protocol
        );

        if (transport->fd < 0)
            continue;

        if (connect(
                transport->fd,
                it->ai_addr,
                it->ai_addrlen
            ) == 0)
        {
            enum AmiAPIError timeout_err;

            timeout_err = apply_socket_timeouts(
                transport
            );

            if (timeout_err == AMIAPI_OK)
                break;
        }

        close(transport->fd);
        transport->fd = -1;
    }

    freeaddrinfo(result);

    if (transport->fd < 0)
        return AMIAPI_ERR_CONNECT;

    return AMIAPI_OK;
}

enum AmiAPIError amiapi_transport_start_tls(
    struct AmiAPITransport *transport,
    const char *host)
{
    SSL_CTX *ctx;
    SSL *ssl;
    X509_VERIFY_PARAM *param;

    if (transport == NULL ||
        transport->fd < 0 ||
        host == NULL ||
        host[0] == '\0')
        return AMIAPI_ERR_INVALID_ARGUMENT;

    if (amiapi_transport_prepare_tls() != AMIAPI_OK)
        return AMIAPI_ERR_CONNECT;

    ctx = SSL_CTX_new(TLS_client_method());
    if (ctx == NULL)
        return AMIAPI_ERR_CONNECT;

    /*
     * Load the operating system's normal CA trust store.
     */
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        SSL_CTX_free(ctx);
        return AMIAPI_ERR_CONNECT;
    }

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    ssl = SSL_new(ctx);
    if (ssl == NULL) {
        SSL_CTX_free(ctx);
        return AMIAPI_ERR_CONNECT;
    }

    /*
     * SNI tells virtual HTTPS servers which hostname we are connecting to.
     */
    if (SSL_set_tlsext_host_name(ssl, host) != 1)
        goto fail;

    /*
     * SSL_VERIFY_PEER validates the certificate chain.  This additionally
     * requires the certificate to be valid for the hostname in the URL.
     */
    param = SSL_get0_param(ssl);
    if (param == NULL)
        goto fail;

    X509_VERIFY_PARAM_set_hostflags(
        param,
        X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS
    );

    if (X509_VERIFY_PARAM_set1_host(param, host, 0) != 1)
        goto fail;

    if (SSL_set_fd(ssl, transport->fd) != 1)
        goto fail;

    if (!set_nonblocking(transport->fd))
        goto fail;

    for (;;) {
        int n;
        int ssl_error;

        n = SSL_connect(ssl);

        if (n == 1)
            break;

        ssl_error = SSL_get_error(ssl, n);

        if (!wait_for_ssl(
                transport,
                ssl_error))
        {
            goto fail;
        }
    }

    if (SSL_get_verify_result(ssl) != X509_V_OK)
        goto fail;

    transport->tls_ctx = ctx;
    transport->tls_ssl = ssl;
    transport->use_tls = 1;

    return AMIAPI_OK;

fail:
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    return AMIAPI_ERR_CONNECT;
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
        if (transport->use_tls) {
            SSL *ssl = (SSL *)transport->tls_ssl;
            int amount;
            int n;
            int ssl_error;

            if (ssl == NULL)
                return AMIAPI_ERR_SEND;

            amount = size - done > (size_t)INT_MAX
                ? INT_MAX
                : (int)(size - done);

            n = SSL_write(
                ssl,
                p + done,
                amount
            );

            if (n > 0) {
                done += (size_t)n;
                continue;
            }

            ssl_error = SSL_get_error(ssl, n);

            if (wait_for_ssl(
                    transport,
                    ssl_error))
            {
                continue;
            }

            return AMIAPI_ERR_SEND;
        }
        else {
            ssize_t n = send(
                transport->fd,
                p + done,
                size - done,
                0
            );

            if (n < 0) {
                if (errno == EINTR)
                    continue;

                return AMIAPI_ERR_SEND;
            }

            if (n == 0)
                return AMIAPI_ERR_SEND;

            done += (size_t)n;
        }
    }

    return AMIAPI_OK;
}

long amiapi_transport_receive(struct AmiAPITransport *transport,
                              void *buffer,
                              size_t size)
{
    if (transport == NULL ||
        transport->fd < 0 ||
        buffer == NULL ||
        size == 0)
        return -1;

    if (transport->use_tls) {
        SSL *ssl = (SSL *)transport->tls_ssl;
        int amount;

        if (ssl == NULL)
            return -1;

        amount = size > (size_t)INT_MAX
            ? INT_MAX
            : (int)size;

        for (;;) {
            int n;
            int ssl_error;

            n = SSL_read(
                ssl,
                buffer,
                amount
            );

            if (n > 0)
                return (long)n;

            ssl_error = SSL_get_error(ssl, n);

            if (ssl_error == SSL_ERROR_ZERO_RETURN)
                return 0;

            if (!wait_for_ssl(
                    transport,
                    ssl_error))
            {
                return -1;
            }
        }
    }
    else {
        ssize_t n;

        do {
            n = recv(transport->fd, buffer, size, 0);
        } while (n < 0 && errno == EINTR);

        return (long)n;
    }
}

void amiapi_transport_close(struct AmiAPITransport *transport)
{
    if (transport == NULL)
        return;

    if (transport->tls_ssl != NULL) {
        SSL *ssl = (SSL *)transport->tls_ssl;

        /*
         * Best-effort TLS close notification.  We deliberately do not
         * block trying to complete a bidirectional shutdown here.
         */
        SSL_shutdown(ssl);
        SSL_free(ssl);
        transport->tls_ssl = NULL;
    }

    if (transport->tls_ctx != NULL) {
        SSL_CTX_free((SSL_CTX *)transport->tls_ctx);
        transport->tls_ctx = NULL;
    }

    transport->use_tls = 0;

    if (transport->fd >= 0) {
        close(transport->fd);
        transport->fd = -1;
    }
}

void amiapi_transport_platform_cleanup(void)
{
}
