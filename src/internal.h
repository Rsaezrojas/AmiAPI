#ifndef AMIAPI_INTERNAL_H
#define AMIAPI_INTERNAL_H

#include <stddef.h>
#include <amiapi/errors.h>

struct AmiAPIContext {
    unsigned long magic;
    size_t max_body;
    enum AmiAPIError last_error;
};

#define AMIAPI_CONTEXT_MAGIC 0x41415049UL

void amiapi_set_error(struct AmiAPIContext *ctx, enum AmiAPIError error);

struct AmiAPITransport {
    int fd;
};

enum AmiAPIError amiapi_transport_connect(struct AmiAPITransport *transport,
                                          const char *host,
                                          unsigned short port);
enum AmiAPIError amiapi_transport_send_all(struct AmiAPITransport *transport,
                                           const void *data,
                                           size_t size);
long amiapi_transport_receive(struct AmiAPITransport *transport,
                              void *buffer,
                              size_t size);
void amiapi_transport_close(struct AmiAPITransport *transport);

/* Release process-global resources owned by the selected platform backend. */
void amiapi_transport_platform_cleanup(void);

#endif
