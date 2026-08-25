#ifndef AMIAPI_JSON_H
#define AMIAPI_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMIAPI_JSON_DEFAULT_MAX_DEPTH 64U

enum AmiAPIJsonType {
    AMIAPI_JSON_NULL = 0,
    AMIAPI_JSON_BOOL,
    AMIAPI_JSON_NUMBER,
    AMIAPI_JSON_STRING,
    AMIAPI_JSON_ARRAY,
    AMIAPI_JSON_OBJECT
};

enum AmiAPIJsonError {
    AMIAPI_JSON_OK = 0,
    AMIAPI_JSON_ERR_INVALID_ARGUMENT,
    AMIAPI_JSON_ERR_NO_MEMORY,
    AMIAPI_JSON_ERR_SYNTAX,
    AMIAPI_JSON_ERR_DEPTH,
    AMIAPI_JSON_ERR_UTF8
};

struct AmiAPIJsonDoc;
struct AmiAPIJsonValue;

struct AmiAPIJsonDoc *AmiAPI_JSONParse(
    const void *data,
    size_t size,
    enum AmiAPIJsonError *error,
    size_t *error_offset
);

void AmiAPI_JSONFree(struct AmiAPIJsonDoc *);

const struct AmiAPIJsonValue *AmiAPI_JSONRoot(
    const struct AmiAPIJsonDoc *
);

enum AmiAPIJsonType AmiAPI_JSONType(
    const struct AmiAPIJsonValue *
);

const struct AmiAPIJsonValue *AmiAPI_JSONObjectGet(
    const struct AmiAPIJsonDoc *,
    const struct AmiAPIJsonValue *,
    const char *
);

size_t AmiAPI_JSONArraySize(
    const struct AmiAPIJsonValue *
);

const struct AmiAPIJsonValue *AmiAPI_JSONArrayGet(
    const struct AmiAPIJsonDoc *,
    const struct AmiAPIJsonValue *,
    size_t
);

int AmiAPI_JSONBool(
    const struct AmiAPIJsonValue *,
    int *
);

enum AmiAPIJsonError AmiAPI_JSONStringCopy(
    const struct AmiAPIJsonDoc *,
    const struct AmiAPIJsonValue *,
    char *,
    size_t,
    size_t *
);

enum AmiAPIJsonError AmiAPI_JSONLong(
    const struct AmiAPIJsonDoc *,
    const struct AmiAPIJsonValue *,
    long *
);

enum AmiAPIJsonError AmiAPI_JSONULong(
    const struct AmiAPIJsonDoc *,
    const struct AmiAPIJsonValue *,
    unsigned long *
);

enum AmiAPIJsonError AmiAPI_JSONNumberToken(
    const struct AmiAPIJsonDoc *,
    const struct AmiAPIJsonValue *,
    const char **,
    size_t *
);

#ifdef __cplusplus
}
#endif

#endif
