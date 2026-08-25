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

static char base64_digit(unsigned int value)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    return alphabet[value & 63U];
}

static enum AmiAPIError base64_encode(
    const unsigned char *data,
    size_t size,
    char **encoded_out)
{
    size_t groups;
    size_t encoded_size;
    char *encoded;
    size_t in;
    size_t out;

    if ((!data && size != 0) || !encoded_out)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    *encoded_out = NULL;

    /*
     * ceil(size / 3) * 4 + terminating NUL,
     * with explicit overflow checks.
     */
    if (size > (size_t)-1 - 2)
        return AMIAPI_ERR_NO_MEMORY;

    groups = (size + 2) / 3;

    if (groups > ((size_t)-1 - 1) / 4)
        return AMIAPI_ERR_NO_MEMORY;

    encoded_size = groups * 4;

    encoded = (char *)malloc(encoded_size + 1);

    if (!encoded)
        return AMIAPI_ERR_NO_MEMORY;

    in = 0;
    out = 0;

    while (size - in >= 3) {
        unsigned int a;
        unsigned int b;
        unsigned int c;

        a = data[in++];
        b = data[in++];
        c = data[in++];

        encoded[out++] =
            base64_digit(a >> 2);

        encoded[out++] =
            base64_digit(
                ((a & 0x03U) << 4) |
                (b >> 4)
            );

        encoded[out++] =
            base64_digit(
                ((b & 0x0fU) << 2) |
                (c >> 6)
            );

        encoded[out++] =
            base64_digit(c);
    }

    if (in < size) {
        unsigned int a;
        unsigned int b;

        a = data[in++];

        encoded[out++] =
            base64_digit(a >> 2);

        if (in < size) {
            b = data[in++];

            encoded[out++] =
                base64_digit(
                    ((a & 0x03U) << 4) |
                    (b >> 4)
                );

            encoded[out++] =
                base64_digit(
                    (b & 0x0fU) << 2
                );

            encoded[out++] = '=';
        }
        else {
            encoded[out++] =
                base64_digit(
                    (a & 0x03U) << 4
                );

            encoded[out++] = '=';
            encoded[out++] = '=';
        }
    }

    encoded[out] = '\0';

    *encoded_out = encoded;
    return AMIAPI_OK;
}

enum AmiAPIError AmiAPI_RequestSetBasicAuth(
    struct AmiAPIRequest *request,
    const char *username,
    const char *password)
{
    static const char prefix[] = "Basic ";
    size_t username_length;
    size_t password_length;
    size_t credentials_size;
    unsigned char *credentials;
    char *encoded;
    char *value;
    size_t encoded_length;
    size_t value_length;
    enum AmiAPIError err;

    if (!request || !username || !password)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    username_length = strlen(username);
    password_length = strlen(password);

    if (strchr(username, '\r') ||
        strchr(username, '\n') ||
        strchr(password, '\r') ||
        strchr(password, '\n'))
    {
        return AMIAPI_ERR_INVALID_ARGUMENT;
    }

    /*
     * RFC 7617 uses user-pass = user-id ":" password.
     * A colon in the username would make the split ambiguous.
     */
    if (strchr(username, ':'))
        return AMIAPI_ERR_INVALID_ARGUMENT;

    if (username_length >
        (size_t)-1 - password_length - 1)
    {
        return AMIAPI_ERR_NO_MEMORY;
    }

    credentials_size =
        username_length +
        1 +
        password_length;

    credentials = (unsigned char *)malloc(
        credentials_size
    );

    if (!credentials && credentials_size != 0)
        return AMIAPI_ERR_NO_MEMORY;

    memcpy(
        credentials,
        username,
        username_length
    );

    credentials[username_length] = ':';

    memcpy(
        credentials + username_length + 1,
        password,
        password_length
    );

    encoded = NULL;

    err = base64_encode(
        credentials,
        credentials_size,
        &encoded
    );

    free(credentials);

    if (err != AMIAPI_OK)
        return err;

    encoded_length = strlen(encoded);

    if (encoded_length >
        (size_t)-1 - (sizeof(prefix) - 1) - 1)
    {
        free(encoded);
        return AMIAPI_ERR_NO_MEMORY;
    }

    value_length =
        (sizeof(prefix) - 1) +
        encoded_length;

    value = (char *)malloc(value_length + 1);

    if (!value) {
        free(encoded);
        return AMIAPI_ERR_NO_MEMORY;
    }

    memcpy(
        value,
        prefix,
        sizeof(prefix) - 1
    );

    memcpy(
        value + sizeof(prefix) - 1,
        encoded,
        encoded_length + 1
    );

    free(encoded);

    err = AmiAPI_RequestSetHeader(
        request,
        "Authorization",
        value
    );

    free(value);

    return err;
}
