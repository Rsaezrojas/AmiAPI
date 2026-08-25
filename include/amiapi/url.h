#ifndef AMIAPI_URL_H
#define AMIAPI_URL_H

#include <stddef.h>

#include <amiapi/errors.h>

#ifdef __cplusplus
extern "C" {
#endif

struct AmiAPIQuery;

enum AmiAPIError AmiAPI_URLEncode(
    const void *,
    size_t,
    char *,
    size_t,
    size_t *
);

struct AmiAPIQuery *AmiAPI_QueryCreate(
    const char *
);

enum AmiAPIError AmiAPI_QueryAdd(
    struct AmiAPIQuery *,
    const char *,
    const char *
);

const char *AmiAPI_QueryURL(
    const struct AmiAPIQuery *
);

void AmiAPI_QueryDestroy(
    struct AmiAPIQuery *
);

#ifdef __cplusplus
}
#endif

#endif
