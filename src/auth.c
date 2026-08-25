#include <stdlib.h>
#include <string.h>

#include <amiapi/auth.h>

enum AmiAPIError AmiAPI_RequestSetBearerToken(
    struct AmiAPIRequest *request,
    const char *token)
{
    static const char prefix[] = "Bearer ";
    size_t prefix_length;
    size_t token_length;
    size_t length;
    char *value;
    enum AmiAPIError err;

    if (!request || !token || token[0] == '\0')
        return AMIAPI_ERR_INVALID_ARGUMENT;

    prefix_length = sizeof(prefix) - 1;
    token_length = strlen(token);

    if (token_length > (size_t)-1 - prefix_length - 1)
        return AMIAPI_ERR_NO_MEMORY;

    length = prefix_length + token_length;

    value = (char *)malloc(length + 1);
    if (!value)
        return AMIAPI_ERR_NO_MEMORY;

    memcpy(value, prefix, prefix_length);
    memcpy(value + prefix_length, token, token_length + 1);

    /*
     * RequestSetHeader performs the canonical request-header
     * validation, including CR/LF rejection, and marks
     * Authorization as sensitive for redirect handling.
     */
    err = AmiAPI_RequestSetHeader(
        request,
        "Authorization",
        value
    );

    /*
     * The request owns its own copy of the header value.
     */
    free(value);

    return err;
}
