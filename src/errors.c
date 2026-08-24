#include <amiapi/errors.h>

const char *AmiAPI_ErrorString(enum AmiAPIError error)
{
    switch (error) {
    case AMIAPI_OK:
        return "success";
    case AMIAPI_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case AMIAPI_ERR_NO_MEMORY:
        return "out of memory";
    case AMIAPI_ERR_INTERNAL:
        return "internal error";
    default:
        return "unknown error";
    }
}
