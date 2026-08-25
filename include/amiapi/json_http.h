#ifndef AMIAPI_JSON_HTTP_H
#define AMIAPI_JSON_HTTP_H

#include <stddef.h>

#include <amiapi/amiapi.h>
#include <amiapi/json.h>

#ifdef __cplusplus
extern "C" {
#endif

int AmiAPI_JSONResponseIsJSON(
    const struct AmiAPIResponse *
);

struct AmiAPIJsonDoc *AmiAPI_JSONParseResponse(
    const struct AmiAPIResponse *,
    enum AmiAPIJsonError *,
    size_t *
);

enum AmiAPIError AmiAPI_RequestSetJSON(
    struct AmiAPIRequest *,
    const void *,
    size_t
);

#ifdef __cplusplus
}
#endif

#endif
