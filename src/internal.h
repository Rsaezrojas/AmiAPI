#ifndef AMIAPI_INTERNAL_H
#define AMIAPI_INTERNAL_H
#include <stddef.h>
#include <amiapi/errors.h>
struct AmiAPIContext { unsigned long magic; size_t max_body; enum AmiAPIError last_error; };
#define AMIAPI_CONTEXT_MAGIC 0x41415049UL
void amiapi_set_error(struct AmiAPIContext *, enum AmiAPIError);
struct AmiAPITransport { int fd; };
enum AmiAPIError amiapi_transport_connect(struct AmiAPITransport *, const char *, unsigned short);
enum AmiAPIError amiapi_transport_send_all(struct AmiAPITransport *, const void *, size_t);
long amiapi_transport_receive(struct AmiAPITransport *, void *, size_t);
void amiapi_transport_close(struct AmiAPITransport *);
void amiapi_transport_platform_cleanup(void);
#endif
