#ifndef AMIAPI_ERRORS_H
#define AMIAPI_ERRORS_H

#ifdef __cplusplus
extern "C" {
#endif

enum AmiAPIError {
    AMIAPI_OK = 0,
    AMIAPI_ERR_INVALID_ARGUMENT = 1,
    AMIAPI_ERR_NO_MEMORY = 2,
    AMIAPI_ERR_INTERNAL = 3
};

const char *AmiAPI_ErrorString(enum AmiAPIError error);

#ifdef __cplusplus
}
#endif

#endif
