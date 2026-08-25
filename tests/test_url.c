#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <amiapi/url.h>

static int expect_encode(
    const void *data,
    size_t size,
    const char *expected)
{
    enum AmiAPIError err;
    size_t required;
    char *buffer;

    required = 0;

    err = AmiAPI_URLEncode(
        data,
        size,
        NULL,
        0,
        &required
    );

    if (err != AMIAPI_OK) {
        fprintf(stderr, "size query failed: %d\n", (int)err);
        return 0;
    }

    if (required != strlen(expected) + 1) {
        fprintf(
            stderr,
            "wrong required size: got %lu expected %lu\n",
            (unsigned long)required,
            (unsigned long)(strlen(expected) + 1)
        );
        return 0;
    }

    buffer = (char *)malloc(required);
    if (!buffer)
        return 0;

    err = AmiAPI_URLEncode(
        data,
        size,
        buffer,
        required,
        &required
    );

    if (err != AMIAPI_OK) {
        fprintf(stderr, "encode failed: %d\n", (int)err);
        free(buffer);
        return 0;
    }

    if (strcmp(buffer, expected) != 0) {
        fprintf(
            stderr,
            "encoding mismatch:\n"
            "  got:      %s\n"
            "  expected: %s\n",
            buffer,
            expected
        );
        free(buffer);
        return 0;
    }

    free(buffer);
    return 1;
}

static int expect_query(
    const char *base,
    const char *name1,
    const char *value1,
    const char *name2,
    const char *value2,
    const char *expected)
{
    struct AmiAPIQuery *query;
    enum AmiAPIError err;
    const char *url;

    query = AmiAPI_QueryCreate(base);
    if (!query) {
        fprintf(stderr, "QueryCreate failed\n");
        return 0;
    }

    err = AmiAPI_QueryAdd(query, name1, value1);

    if (err != AMIAPI_OK) {
        fprintf(stderr, "first QueryAdd failed: %d\n", (int)err);
        AmiAPI_QueryDestroy(query);
        return 0;
    }

    if (name2) {
        err = AmiAPI_QueryAdd(query, name2, value2);

        if (err != AMIAPI_OK) {
            fprintf(stderr, "second QueryAdd failed: %d\n", (int)err);
            AmiAPI_QueryDestroy(query);
            return 0;
        }
    }

    url = AmiAPI_QueryURL(query);

    if (!url || strcmp(url, expected) != 0) {
        fprintf(
            stderr,
            "query mismatch:\n"
            "  got:      %s\n"
            "  expected: %s\n",
            url ? url : "(null)",
            expected
        );

        AmiAPI_QueryDestroy(query);
        return 0;
    }

    AmiAPI_QueryDestroy(query);
    return 1;
}

int main(void)
{
    static const unsigned char binary[] = {
        0x00, 0x01, 0x20, 0x7f, 0x80, 0xff
    };

    static const unsigned char utf8[] = {
        0xc3, 0xb8,       /* ø */
        0xe2, 0x82, 0xac /* euro */
    };

    char tiny[2];
    size_t required;
    enum AmiAPIError err;

    if (!expect_encode(
            "AZaz09-._~",
            strlen("AZaz09-._~"),
            "AZaz09-._~"))
        return 1;

    if (!expect_encode(
            "Amiga 1200",
            strlen("Amiga 1200"),
            "Amiga%201200"))
        return 1;

    if (!expect_encode(
            "a/b?c=d&e",
            strlen("a/b?c=d&e"),
            "a%2Fb%3Fc%3Dd%26e"))
        return 1;

    if (!expect_encode(
            binary,
            sizeof(binary),
            "%00%01%20%7F%80%FF"))
        return 1;

    if (!expect_encode(
            utf8,
            sizeof(utf8),
            "%C3%B8%E2%82%AC"))
        return 1;

    if (!expect_encode(
            "",
            0,
            ""))
        return 1;

    required = 0;

    err = AmiAPI_URLEncode(
        "abc",
        3,
        tiny,
        sizeof(tiny),
        &required
    );

    if (err != AMIAPI_ERR_NO_MEMORY ||
        required != 4 ||
        tiny[0] != '\0')
    {
        fprintf(stderr, "small-buffer handling failed\n");
        return 1;
    }

    if (!expect_query(
            "https://api.example/search",
            "q",
            "Amiga 1200",
            NULL,
            NULL,
            "https://api.example/search?q=Amiga%201200"))
        return 1;

    if (!expect_query(
            "https://api.example/search?page=2",
            "q",
            "demo",
            NULL,
            NULL,
            "https://api.example/search?page=2&q=demo"))
        return 1;

    if (!expect_query(
            "https://api.example/search",
            "q",
            "Amiga",
            "sort",
            "rating desc",
            "https://api.example/search?q=Amiga&sort=rating%20desc"))
        return 1;

    if (!expect_query(
            "https://api.example/search#results",
            "q",
            "demo",
            NULL,
            NULL,
            "https://api.example/search?q=demo#results"))
        return 1;

    if (!expect_query(
            "https://api.example/search?page=2#results",
            "q",
            "demo",
            NULL,
            NULL,
            "https://api.example/search?page=2&q=demo#results"))
        return 1;

    if (!expect_query(
            "https://api.example/search",
            "empty",
            "",
            NULL,
            NULL,
            "https://api.example/search?empty="))
        return 1;

    if (!expect_query(
            "https://api.example/search",
            "weird name",
            "a&b=c",
            NULL,
            NULL,
            "https://api.example/search?weird%20name=a%26b%3Dc"))
        return 1;

    puts("URL percent-encoding tests: OK");
    puts("URL query-builder tests: OK");

    return 0;
}
