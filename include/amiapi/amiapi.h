#ifndef AMIAPI_AMIAPI_H
#define AMIAPI_AMIAPI_H

#ifdef __cplusplus
extern "C" {
#endif

#define AMIAPI_VERSION_MAJOR 0
#define AMIAPI_VERSION_MINOR 0
#define AMIAPI_VERSION_PATCH 1

struct AmiAPIContext;

/*
 * Create an AmiAPI context.
 *
 * M0 has no configuration options yet. The opaque context allows the public
 * API to grow without exposing implementation details.
 */
struct AmiAPIContext *AmiAPI_Create(void);

/* Destroy a context returned by AmiAPI_Create(). NULL is accepted. */
void AmiAPI_Destroy(struct AmiAPIContext *ctx);

/* Return a static, NUL-terminated version string. */
const char *AmiAPI_VersionString(void);

#ifdef __cplusplus
}
#endif

#endif
