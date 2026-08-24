#ifndef AMIAPI_AMIAPI_H
#define AMIAPI_AMIAPI_H

#include <stddef.h>
#include <amiapi/errors.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMIAPI_VERSION_MAJOR 0
#define AMIAPI_VERSION_MINOR 1
#define AMIAPI_VERSION_PATCH 1

#define AMIAPI_DEFAULT_MAX_BODY (1024UL * 1024UL)

struct AmiAPIContext;
struct AmiAPIResponse;

struct AmiAPIContext *AmiAPI_Create(void);
void AmiAPI_Destroy(struct AmiAPIContext *ctx);
const char *AmiAPI_VersionString(void);

void AmiAPI_SetMaxBody(struct AmiAPIContext *ctx, size_t max_body);
enum AmiAPIError AmiAPI_LastError(const struct AmiAPIContext *ctx);

struct AmiAPIResponse *AmiAPI_Get(struct AmiAPIContext *ctx, const char *url);
void AmiAPI_FreeResponse(struct AmiAPIResponse *response);

int AmiAPI_Status(const struct AmiAPIResponse *response);
const unsigned char *AmiAPI_Body(const struct AmiAPIResponse *response);
size_t AmiAPI_BodySize(const struct AmiAPIResponse *response);
const char *AmiAPI_Header(const struct AmiAPIResponse *response,
                          const char *name);

#ifdef __cplusplus
}
#endif

#endif
