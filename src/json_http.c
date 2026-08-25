#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include <amiapi/amiapi.h>
#include <amiapi/json.h>
#include <amiapi/json_http.h>

static int ascii_equal_ci_n(
    const char *a,
    const char *b,
    size_t length)
{
    size_t i;

    for (i = 0; i < length; ++i) {
        unsigned char ca;
        unsigned char cb;

        ca = (unsigned char)a[i];
        cb = (unsigned char)b[i];

        if (tolower((int)ca) != tolower((int)cb))
            return 0;
    }

    return 1;
}

int AmiAPI_JSONResponseIsJSON(
    const struct AmiAPIResponse *response)
{
    const char *content_type;
    const char *end;
    const char *slash;
    size_t length;

    if (!response)
        return 0;

    content_type = AmiAPI_Header(
        response,
        "Content-Type"
    );

    if (!content_type)
        return 0;

    while (*content_type == ' ' ||
           *content_type == '\t')
    {
        ++content_type;
    }

    end = content_type;

    while (*end && *end != ';')
        ++end;

    while (end > content_type &&
           (end[-1] == ' ' || end[-1] == '\t'))
    {
        --end;
    }

    length = (size_t)(end - content_type);

    if (length == strlen("application/json") &&
        ascii_equal_ci_n(
            content_type,
            "application/json",
            length))
    {
        return 1;
    }

    /*
     * RFC 6839 structured syntax suffix:
     *
     *     application/problem+json
     *     application/vnd.example+json
     */
    slash = memchr(content_type, '/', length);

    if (!slash ||
        !ascii_equal_ci_n(
            content_type,
            "application/",
            strlen("application/")))
    {
        return 0;
    }

    if (length < strlen("application/x+json"))
        return 0;

    if (!ascii_equal_ci_n(
            content_type + length - 5,
            "+json",
            5))
    {
        return 0;
    }

    /*
     * Require something between "application/" and "+json".
     */
    if ((size_t)(
            content_type + length - 5 -
            (slash + 1)) == 0)
    {
        return 0;
    }

    return 1;
}

struct AmiAPIJsonDoc *AmiAPI_JSONParseResponse(
    const struct AmiAPIResponse *response,
    enum AmiAPIJsonError *error,
    size_t *error_offset)
{
    if (!response) {
        if (error)
            *error = AMIAPI_JSON_ERR_INVALID_ARGUMENT;

        if (error_offset)
            *error_offset = 0;

        return NULL;
    }

    return AmiAPI_JSONParse(
        AmiAPI_Body(response),
        AmiAPI_BodySize(response),
        error,
        error_offset
    );
}

enum AmiAPIError AmiAPI_RequestSetJSON(
    struct AmiAPIRequest *request,
    const void *data,
    size_t size)
{
    enum AmiAPIError err;

    if (!request || (!data && size != 0))
        return AMIAPI_ERR_INVALID_ARGUMENT;

    err = AmiAPI_RequestSetBody(
        request,
        data,
        size
    );

    if (err != AMIAPI_OK)
        return err;

    err = AmiAPI_RequestSetHeader(
        request,
        "Content-Type",
        "application/json"
    );

    return err;
}
