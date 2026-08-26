#include <amiapi/errors.h>

const char *AmiAPI_ErrorString(enum AmiAPIError error)
{
    switch (error) {
    case AMIAPI_OK: return "success";
    case AMIAPI_ERR_INVALID_ARGUMENT: return "invalid argument";
    case AMIAPI_ERR_NO_MEMORY: return "out of memory";
    case AMIAPI_ERR_INTERNAL: return "internal error";
    case AMIAPI_ERR_BAD_URL: return "bad URL";
    case AMIAPI_ERR_UNSUPPORTED_SCHEME: return "unsupported URL scheme";
    case AMIAPI_ERR_DNS: return "DNS lookup failed";
    case AMIAPI_ERR_CONNECT: return "connection failed";
    case AMIAPI_ERR_SEND: return "send failed";
    case AMIAPI_ERR_RECEIVE: return "receive failed";
    case AMIAPI_ERR_PROTOCOL: return "HTTP protocol error";
    case AMIAPI_ERR_RESPONSE_TOO_LARGE: return "HTTP response body too large";
    case AMIAPI_ERR_SOCKET_LIBRARY: return "unable to open bsdsocket.library";
    case AMIAPI_ERR_HTTP_STATUS: return "HTTP response status is not successful";
    case AMIAPI_ERR_FILESYSTEM: return "filesystem operation failed";
    case AMIAPI_ERR_CANCELLED: return "request cancelled";
    default: return "unknown error";
    }
}
