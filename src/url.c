#include <stdlib.h>
#include <string.h>

#include <amiapi/url.h>

struct AmiAPIQuery {
    char *url;
    size_t length;
    size_t capacity;
    size_t fragment_offset;
};

static int is_unreserved(unsigned char c)
{
    return
        (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') ||
        c == '-' ||
        c == '.' ||
        c == '_' ||
        c == '~';
}

static char hex_digit(unsigned int value)
{
    static const char digits[] = "0123456789ABCDEF";

    return digits[value & 15U];
}

enum AmiAPIError AmiAPI_URLEncode(
    const void *data,
    size_t size,
    char *buffer,
    size_t buffer_size,
    size_t *required_size)
{
    const unsigned char *input;
    size_t needed;
    size_t i;
    size_t out;

    if ((!data && size != 0) ||
        !required_size ||
        (!buffer && buffer_size != 0))
    {
        return AMIAPI_ERR_INVALID_ARGUMENT;
    }

    input = (const unsigned char *)data;
    needed = 1;

    for (i = 0; i < size; ++i) {
        size_t add = is_unreserved(input[i]) ? 1 : 3;

        if (needed > (size_t)-1 - add)
            return AMIAPI_ERR_NO_MEMORY;

        needed += add;
    }

    *required_size = needed;

    if (!buffer)
        return AMIAPI_OK;

    if (buffer_size < needed) {
        if (buffer_size > 0)
            buffer[0] = '\0';

        return AMIAPI_ERR_NO_MEMORY;
    }

    out = 0;

    for (i = 0; i < size; ++i) {
        unsigned char c = input[i];

        if (is_unreserved(c)) {
            buffer[out++] = (char)c;
        }
        else {
            buffer[out++] = '%';
            buffer[out++] = hex_digit(c >> 4);
            buffer[out++] = hex_digit(c);
        }
    }

    buffer[out] = '\0';

    return AMIAPI_OK;
}

static enum AmiAPIError ensure_capacity(
    struct AmiAPIQuery *query,
    size_t needed)
{
    char *new_url;
    size_t capacity;

    if (needed <= query->capacity)
        return AMIAPI_OK;

    capacity = query->capacity
        ? query->capacity
        : 64;

    while (capacity < needed) {
        if (capacity > ((size_t)-1) / 2) {
            capacity = needed;
            break;
        }

        capacity *= 2;
    }

    new_url = (char *)realloc(
        query->url,
        capacity
    );

    if (!new_url)
        return AMIAPI_ERR_NO_MEMORY;

    query->url = new_url;
    query->capacity = capacity;

    return AMIAPI_OK;
}

struct AmiAPIQuery *AmiAPI_QueryCreate(
    const char *base_url)
{
    struct AmiAPIQuery *query;
    const char *fragment;
    size_t length;

    if (!base_url)
        return NULL;

    length = strlen(base_url);

    query = (struct AmiAPIQuery *)calloc(
        1,
        sizeof(*query)
    );

    if (!query)
        return NULL;

    query->url = (char *)malloc(length + 1);

    if (!query->url) {
        free(query);
        return NULL;
    }

    memcpy(
        query->url,
        base_url,
        length + 1
    );

    query->length = length;
    query->capacity = length + 1;

    fragment = strchr(base_url, '#');

    query->fragment_offset = fragment
        ? (size_t)(fragment - base_url)
        : length;

    return query;
}

enum AmiAPIError AmiAPI_QueryAdd(
    struct AmiAPIQuery *query,
    const char *name,
    const char *value)
{
    size_t encoded_name_size;
    size_t encoded_value_size;
    char *encoded_name;
    char *encoded_value;
    size_t prefix_length;
    size_t fragment_length;
    char separator;
    size_t needed;
    enum AmiAPIError err;

    if (!query || !name || !value)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    err = AmiAPI_URLEncode(
        name,
        strlen(name),
        NULL,
        0,
        &encoded_name_size
    );

    if (err != AMIAPI_OK)
        return err;

    err = AmiAPI_URLEncode(
        value,
        strlen(value),
        NULL,
        0,
        &encoded_value_size
    );

    if (err != AMIAPI_OK)
        return err;

    encoded_name = (char *)malloc(encoded_name_size);
    encoded_value = (char *)malloc(encoded_value_size);

    if (!encoded_name || !encoded_value) {
        free(encoded_name);
        free(encoded_value);
        return AMIAPI_ERR_NO_MEMORY;
    }

    err = AmiAPI_URLEncode(
        name,
        strlen(name),
        encoded_name,
        encoded_name_size,
        &encoded_name_size
    );

    if (err != AMIAPI_OK)
        goto done;

    err = AmiAPI_URLEncode(
        value,
        strlen(value),
        encoded_value,
        encoded_value_size,
        &encoded_value_size
    );

    if (err != AMIAPI_OK)
        goto done;

    prefix_length = query->fragment_offset;
    fragment_length =
        query->length - query->fragment_offset;

    separator =
        memchr(query->url, '?', prefix_length)
            ? '&'
            : '?';

    if (prefix_length > (size_t)-1 -
            (encoded_name_size - 1) -
            (encoded_value_size - 1) -
            fragment_length -
            4)
    {
        err = AMIAPI_ERR_NO_MEMORY;
        goto done;
    }

    needed =
        prefix_length +
        1 +
        (encoded_name_size - 1) +
        1 +
        (encoded_value_size - 1) +
        fragment_length +
        1;

    err = ensure_capacity(query, needed);

    if (err != AMIAPI_OK)
        goto done;

    /*
     * Move fragment, including its terminating NUL, out of the way.
     */
    memmove(
        query->url +
            prefix_length +
            1 +
            (encoded_name_size - 1) +
            1 +
            (encoded_value_size - 1),
        query->url + prefix_length,
        fragment_length + 1
    );

    query->url[prefix_length++] = separator;

    memcpy(
        query->url + prefix_length,
        encoded_name,
        encoded_name_size - 1
    );

    prefix_length += encoded_name_size - 1;
    query->url[prefix_length++] = '=';

    memcpy(
        query->url + prefix_length,
        encoded_value,
        encoded_value_size - 1
    );

    prefix_length += encoded_value_size - 1;

    query->fragment_offset = prefix_length;
    query->length = needed - 1;

done:
    free(encoded_name);
    free(encoded_value);

    return err;
}

const char *AmiAPI_QueryURL(
    const struct AmiAPIQuery *query)
{
    return query ? query->url : NULL;
}

void AmiAPI_QueryDestroy(
    struct AmiAPIQuery *query)
{
    if (!query)
        return;

    free(query->url);
    free(query);
}
