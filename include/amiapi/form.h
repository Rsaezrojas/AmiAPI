#ifndef AMIAPI_FORM_H
#define AMIAPI_FORM_H

#include <amiapi/amiapi.h>

#ifdef __cplusplus
extern "C" {
#endif

struct AmiAPIForm;

struct AmiAPIForm *AmiAPI_FormCreate(void);

enum AmiAPIError AmiAPI_FormAdd(
    struct AmiAPIForm *,
    const char *,
    const char *
);

const char *AmiAPI_FormBody(
    const struct AmiAPIForm *
);

size_t AmiAPI_FormSize(
    const struct AmiAPIForm *
);

enum AmiAPIError AmiAPI_RequestSetForm(
    struct AmiAPIRequest *,
    const struct AmiAPIForm *
);

void AmiAPI_FormDestroy(
    struct AmiAPIForm *
);

#ifdef __cplusplus
}
#endif

#endif
