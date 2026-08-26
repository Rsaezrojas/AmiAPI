#ifndef AMIAPI_INTERNAL_H
#define AMIAPI_INTERNAL_H
#include <stddef.h>
#include <amiapi/errors.h>
struct AmiAPIContext {
    unsigned long magic;
    size_t max_body;
    unsigned int timeout_seconds;
    enum AmiAPIError last_error;
};
#define AMIAPI_CONTEXT_MAGIC 0x41415049UL
void amiapi_set_error(struct AmiAPIContext *, enum AmiAPIError);
struct AmiAPITransport {
    int fd;
    void *tls_ctx;
    void *tls_ssl;
    int use_tls;
    unsigned int timeout_seconds;
};
enum AmiAPIError amiapi_transport_connect(struct AmiAPITransport *, const char *, unsigned short);
enum AmiAPIError amiapi_transport_prepare_tls(void);
enum AmiAPIError amiapi_transport_start_tls(struct AmiAPITransport *, const char *);
enum AmiAPIError amiapi_transport_send_all(struct AmiAPITransport *, const void *, size_t);
long amiapi_transport_receive(struct AmiAPITransport *, void *, size_t);
void amiapi_transport_close(struct AmiAPITransport *);
void amiapi_transport_platform_cleanup(void);
#endif
