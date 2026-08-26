#ifndef AMIAPI_AMIAPI_H
#define AMIAPI_AMIAPI_H
#include <stddef.h>
#include <amiapi/errors.h>
#ifdef __cplusplus
extern "C" {
#endif
#define AMIAPI_VERSION_MAJOR 0
#define AMIAPI_VERSION_MINOR 1
#define AMIAPI_VERSION_PATCH 2
#define AMIAPI_DEFAULT_MAX_BODY (1024UL * 1024UL)
#define AMIAPI_DEFAULT_TIMEOUT_SECONDS 30U
struct AmiAPIContext;
struct AmiAPIRequest;
struct AmiAPIResponse;

typedef enum AmiAPIError (*AmiAPIBodyCallback)(
    const void *data,
    size_t size,
    void *userdata
);

/*
 * Called after decoded bytes are accepted. received is cumulative; total is
 * validated Content-Length or zero when unknown. Nonzero continues and zero
 * cooperatively cancels the request.
 */
typedef int (*AmiAPIProgressCallback)(
    size_t received,
    size_t total,
    void *userdata
);

struct AmiAPIContext *AmiAPI_Create(void);
void AmiAPI_Destroy(struct AmiAPIContext *);
const char *AmiAPI_VersionString(void);
void AmiAPI_SetMaxBody(struct AmiAPIContext *, size_t);
void AmiAPI_SetTimeout(struct AmiAPIContext *, unsigned int);
unsigned int AmiAPI_Timeout(const struct AmiAPIContext *);
enum AmiAPIError AmiAPI_LastError(const struct AmiAPIContext *);

struct AmiAPIRequest *AmiAPI_RequestCreate(
    struct AmiAPIContext *,
    const char *,
    const char *
);
enum AmiAPIError AmiAPI_RequestSetHeader(
    struct AmiAPIRequest *,
    const char *,
    const char *
);
enum AmiAPIError AmiAPI_RequestSetBody(
    struct AmiAPIRequest *,
    const void *,
    size_t
);
enum AmiAPIError AmiAPI_RequestSetResponseCallback(
    struct AmiAPIRequest *,
    AmiAPIBodyCallback,
    void *
);
enum AmiAPIError AmiAPI_RequestSetProgressCallback(
    struct AmiAPIRequest *,
    AmiAPIProgressCallback,
    void *
);
enum AmiAPIError AmiAPI_RequestDownloadFile(
    struct AmiAPIRequest *,
    const char *
);
void AmiAPI_RequestClearHeaders(struct AmiAPIRequest *);
struct AmiAPIResponse *AmiAPI_RequestExecute(struct AmiAPIRequest *);
void AmiAPI_RequestDestroy(struct AmiAPIRequest *);

struct AmiAPIResponse *AmiAPI_Get(struct AmiAPIContext *, const char *);
enum AmiAPIError AmiAPI_DownloadFile(
    struct AmiAPIContext *,
    const char *,
    const char *
);
void AmiAPI_FreeResponse(struct AmiAPIResponse *);
int AmiAPI_Status(const struct AmiAPIResponse *);
const unsigned char *AmiAPI_Body(const struct AmiAPIResponse *);
size_t AmiAPI_BodySize(const struct AmiAPIResponse *);
/*
 * Case-insensitive lookup returns the first occurrence, or NULL. Returned
 * storage belongs to the response and ends with AmiAPI_FreeResponse().
 */
const char *AmiAPI_Header(const struct AmiAPIResponse *, const char *);
#ifdef __cplusplus
}
#endif
#endif
