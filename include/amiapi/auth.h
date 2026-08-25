#ifndef AMIAPI_AUTH_H
#define AMIAPI_AUTH_H

#include <amiapi/amiapi.h>

#ifdef __cplusplus
extern "C" {
#endif

enum AmiAPIError AmiAPI_RequestSetBearerToken(
    struct AmiAPIRequest *,
    const char *
);

enum AmiAPIError AmiAPI_RequestSetBasicAuth(
    struct AmiAPIRequest *,
    const char *,
    const char *
);

#ifdef __cplusplus
}
#endif

#endif
