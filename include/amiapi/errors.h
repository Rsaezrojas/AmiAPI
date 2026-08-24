#ifndef AMIAPI_ERRORS_H
#define AMIAPI_ERRORS_H

#ifdef __cplusplus
extern "C" {
#endif

enum AmiAPIError {
    AMIAPI_OK = 0,
    AMIAPI_ERR_INVALID_ARGUMENT,
    AMIAPI_ERR_NO_MEMORY,
    AMIAPI_ERR_INTERNAL,
    AMIAPI_ERR_BAD_URL,
    AMIAPI_ERR_UNSUPPORTED_SCHEME,
    AMIAPI_ERR_DNS,
    AMIAPI_ERR_CONNECT,
    AMIAPI_ERR_SEND,
    AMIAPI_ERR_RECEIVE,
    AMIAPI_ERR_PROTOCOL,
    AMIAPI_ERR_RESPONSE_TOO_LARGE
};

const char *AmiAPI_ErrorString(enum AmiAPIError error);

#ifdef __cplusplus
}
#endif

#endif
