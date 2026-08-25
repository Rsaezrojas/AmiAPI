#include <stdio.h>
#include <string.h>

#include <amiapi/json.h>

struct TestCase {
    const char *json;
    int should_pass;
    enum AmiAPIJsonType root_type;
};

static int expect_bytes(
    const unsigned char *data,
    size_t size,
    int should_pass,
    enum AmiAPIJsonError expected_error,
    const char *label)
{
    struct AmiAPIJsonDoc *doc;
    enum AmiAPIJsonError error;
    size_t offset;

    error = AMIAPI_JSON_OK;
    offset = 0;

    doc = AmiAPI_JSONParse(
        data,
        size,
        &error,
        &offset
    );

    if (should_pass) {
        if (!doc) {
            fprintf(
                stderr,
                "FAIL %s: error=%d offset=%lu\n",
                label,
                (int)error,
                (unsigned long)offset
            );
            return 0;
        }

        AmiAPI_JSONFree(doc);
        return 1;
    }

    if (doc) {
        fprintf(
            stderr,
            "FAIL %s: invalid JSON accepted\n",
            label
        );

        AmiAPI_JSONFree(doc);
        return 0;
    }

    if (expected_error != AMIAPI_JSON_OK &&
        error != expected_error)
    {
        fprintf(
            stderr,
            "FAIL %s: expected error=%d got=%d "
            "offset=%lu\n",
            label,
            (int)expected_error,
            (int)error,
            (unsigned long)offset
        );
        return 0;
    }

    return 1;
}

static int test_unicode(void)
{
    static const unsigned char valid_utf8[] = {
        '"',
        'A',
        0xc3, 0xb8,              /* ø */
        0xe2, 0x82, 0xac,        /* euro */
        0xf0, 0x9f, 0x98, 0x80,  /* U+1F600 */
        '"'
    };

    static const unsigned char invalid_overlong[] = {
        '"', 0xc0, 0xaf, '"'
    };

    static const unsigned char invalid_continuation[] = {
        '"', 0xe2, 0x28, 0xa1, '"'
    };

    static const unsigned char invalid_surrogate_utf8[] = {
        '"', 0xed, 0xa0, 0x80, '"'
    };

    static const unsigned char invalid_too_high[] = {
        '"', 0xf4, 0x90, 0x80, 0x80, '"'
    };

    static const unsigned char embedded_nul[] = {
        '"', 'a', 0x00, 'b', '"'
    };

    if (!expect_bytes(
            valid_utf8,
            sizeof(valid_utf8),
            1,
            AMIAPI_JSON_OK,
            "valid raw UTF-8"))
        return 0;

    if (!expect_bytes(
            invalid_overlong,
            sizeof(invalid_overlong),
            0,
            AMIAPI_JSON_ERR_UTF8,
            "overlong UTF-8"))
        return 0;

    if (!expect_bytes(
            invalid_continuation,
            sizeof(invalid_continuation),
            0,
            AMIAPI_JSON_ERR_UTF8,
            "bad UTF-8 continuation"))
        return 0;

    if (!expect_bytes(
            invalid_surrogate_utf8,
            sizeof(invalid_surrogate_utf8),
            0,
            AMIAPI_JSON_ERR_UTF8,
            "UTF-8 surrogate"))
        return 0;

    if (!expect_bytes(
            invalid_too_high,
            sizeof(invalid_too_high),
            0,
            AMIAPI_JSON_ERR_UTF8,
            "UTF-8 above U+10FFFF"))
        return 0;

    if (!expect_bytes(
            embedded_nul,
            sizeof(embedded_nul),
            0,
            AMIAPI_JSON_ERR_SYNTAX,
            "embedded NUL"))
        return 0;

    if (!expect_bytes(
            (const unsigned char *)"\"\\u00f8\"",
            strlen("\"\\u00f8\""),
            1,
            AMIAPI_JSON_OK,
            "BMP unicode escape"))
        return 0;

    if (!expect_bytes(
            (const unsigned char *)"\"\\ud83d\\ude00\"",
            strlen("\"\\ud83d\\ude00\""),
            1,
            AMIAPI_JSON_OK,
            "surrogate pair"))
        return 0;

    if (!expect_bytes(
            (const unsigned char *)"\"\\ud83d\"",
            strlen("\"\\ud83d\""),
            0,
            AMIAPI_JSON_ERR_UTF8,
            "unpaired high surrogate"))
        return 0;

    if (!expect_bytes(
            (const unsigned char *)"\"\\ude00\"",
            strlen("\"\\ude00\""),
            0,
            AMIAPI_JSON_ERR_UTF8,
            "unpaired low surrogate"))
        return 0;

    if (!expect_bytes(
            (const unsigned char *)"\"\\ud83d\\u0041\"",
            strlen("\"\\ud83d\\u0041\""),
            0,
            AMIAPI_JSON_ERR_UTF8,
            "high surrogate plus non-low surrogate"))
        return 0;

    if (!expect_bytes(
            (const unsigned char *)"\"\\u12xz\"",
            strlen("\"\\u12xz\""),
            0,
            AMIAPI_JSON_ERR_SYNTAX,
            "malformed unicode escape"))
        return 0;

    return 1;
}

static int test_depth(void)
{
    unsigned char valid[2 * AMIAPI_JSON_DEFAULT_MAX_DEPTH + 5];
    unsigned char invalid[2 * (AMIAPI_JSON_DEFAULT_MAX_DEPTH + 1) + 5];
    size_t i;
    size_t pos;

    /*
     * Exactly 64 nested arrays is allowed.
     */
    pos = 0;

    for (i = 0; i < AMIAPI_JSON_DEFAULT_MAX_DEPTH; ++i)
        valid[pos++] = '[';

    valid[pos++] = '0';

    for (i = 0; i < AMIAPI_JSON_DEFAULT_MAX_DEPTH; ++i)
        valid[pos++] = ']';

    if (!expect_bytes(
            valid,
            pos,
            1,
            AMIAPI_JSON_OK,
            "maximum valid JSON depth"))
        return 0;

    /*
     * One level deeper must fail deterministically.
     */
    pos = 0;

    for (i = 0; i < AMIAPI_JSON_DEFAULT_MAX_DEPTH + 1; ++i)
        invalid[pos++] = '[';

    invalid[pos++] = '0';

    for (i = 0; i < AMIAPI_JSON_DEFAULT_MAX_DEPTH + 1; ++i)
        invalid[pos++] = ']';

    if (!expect_bytes(
            invalid,
            pos,
            0,
            AMIAPI_JSON_ERR_DEPTH,
            "JSON depth limit"))
        return 0;

    return 1;
}

static int test_query_api(void)
{
    static const char json[] =
        "{"
        "\"name\":\"Ami\\u0041PI\","
        "\"ok\":true,"
        "\"count\":12345,"
        "\"negative\":-42,"
        "\"fraction\":1.5,"
        "\"items\":[10,20,30],"
        "\"escaped\\u005fkey\":\"yes\","
        "\"emoji\":\"\\ud83d\\ude00\""
        "}";

    struct AmiAPIJsonDoc *doc;
    const struct AmiAPIJsonValue *root;
    const struct AmiAPIJsonValue *value;
    const struct AmiAPIJsonValue *items;
    enum AmiAPIJsonError error;
    size_t offset;
    size_t required;
    char buffer[64];
    long signed_value;
    unsigned long unsigned_value;
    int boolean;
    const char *token;
    size_t token_length;

    error = AMIAPI_JSON_OK;
    offset = 0;

    doc = AmiAPI_JSONParse(
        json,
        strlen(json),
        &error,
        &offset
    );

    if (!doc) {
        fprintf(
            stderr,
            "FAIL query JSON parse error=%d offset=%lu\n",
            (int)error,
            (unsigned long)offset
        );
        return 0;
    }

    root = AmiAPI_JSONRoot(doc);

    value = AmiAPI_JSONObjectGet(doc, root, "name");

    required = 0;

    if (!value ||
        AmiAPI_JSONStringCopy(
            doc,
            value,
            buffer,
            sizeof(buffer),
            &required) != AMIAPI_JSON_OK ||
        strcmp(buffer, "AmiAPI") != 0)
    {
        fprintf(stderr, "FAIL decoded name\n");
        AmiAPI_JSONFree(doc);
        return 0;
    }

    value = AmiAPI_JSONObjectGet(doc, root, "ok");

    if (!value ||
        !AmiAPI_JSONBool(value, &boolean) ||
        boolean != 1)
    {
        fprintf(stderr, "FAIL bool accessor\n");
        AmiAPI_JSONFree(doc);
        return 0;
    }

    value = AmiAPI_JSONObjectGet(doc, root, "count");

    if (!value ||
        AmiAPI_JSONULong(
            doc,
            value,
            &unsigned_value) != AMIAPI_JSON_OK ||
        unsigned_value != 12345UL)
    {
        fprintf(stderr, "FAIL unsigned integer accessor\n");
        AmiAPI_JSONFree(doc);
        return 0;
    }

    value = AmiAPI_JSONObjectGet(doc, root, "negative");

    if (!value ||
        AmiAPI_JSONLong(
            doc,
            value,
            &signed_value) != AMIAPI_JSON_OK ||
        signed_value != -42L)
    {
        fprintf(stderr, "FAIL signed integer accessor\n");
        AmiAPI_JSONFree(doc);
        return 0;
    }

    value = AmiAPI_JSONObjectGet(doc, root, "fraction");

    if (!value ||
        AmiAPI_JSONLong(
            doc,
            value,
            &signed_value) == AMIAPI_JSON_OK)
    {
        fprintf(stderr, "FAIL fractional integer rejection\n");
        AmiAPI_JSONFree(doc);
        return 0;
    }

    if (AmiAPI_JSONNumberToken(
            doc,
            value,
            &token,
            &token_length) != AMIAPI_JSON_OK ||
        token_length != 3 ||
        memcmp(token, "1.5", 3) != 0)
    {
        fprintf(stderr, "FAIL number token accessor\n");
        AmiAPI_JSONFree(doc);
        return 0;
    }

    items = AmiAPI_JSONObjectGet(doc, root, "items");

    if (!items ||
        AmiAPI_JSONArraySize(items) != 3)
    {
        fprintf(stderr, "FAIL array size\n");
        AmiAPI_JSONFree(doc);
        return 0;
    }

    value = AmiAPI_JSONArrayGet(doc, items, 1);

    if (!value ||
        AmiAPI_JSONLong(
            doc,
            value,
            &signed_value) != AMIAPI_JSON_OK ||
        signed_value != 20L)
    {
        fprintf(stderr, "FAIL array access\n");
        AmiAPI_JSONFree(doc);
        return 0;
    }

    if (AmiAPI_JSONArrayGet(doc, items, 99) != NULL) {
        fprintf(stderr, "FAIL array bounds\n");
        AmiAPI_JSONFree(doc);
        return 0;
    }

    value = AmiAPI_JSONObjectGet(
        doc,
        root,
        "escaped_key"
    );

    if (!value) {
        fprintf(stderr, "FAIL escaped object-key lookup\n");
        AmiAPI_JSONFree(doc);
        return 0;
    }

    value = AmiAPI_JSONObjectGet(doc, root, "emoji");

    required = 0;

    if (!value ||
        AmiAPI_JSONStringCopy(
            doc,
            value,
            buffer,
            sizeof(buffer),
            &required) != AMIAPI_JSON_OK ||
        required != 5 ||
        (unsigned char)buffer[0] != 0xf0 ||
        (unsigned char)buffer[1] != 0x9f ||
        (unsigned char)buffer[2] != 0x98 ||
        (unsigned char)buffer[3] != 0x80 ||
        buffer[4] != '\0')
    {
        fprintf(stderr, "FAIL surrogate string decode\n");
        AmiAPI_JSONFree(doc);
        return 0;
    }

    required = 0;

    if (AmiAPI_JSONStringCopy(
            doc,
            value,
            NULL,
            0,
            &required) != AMIAPI_JSON_OK ||
        required != 5)
    {
        fprintf(stderr, "FAIL string size query\n");
        AmiAPI_JSONFree(doc);
        return 0;
    }

    AmiAPI_JSONFree(doc);
    return 1;
}

int main(void)
{
    static const struct TestCase cases[] = {
        { "null", 1, AMIAPI_JSON_NULL },
        { "true", 1, AMIAPI_JSON_BOOL },
        { "false", 1, AMIAPI_JSON_BOOL },
        { "123", 1, AMIAPI_JSON_NUMBER },
        { "-12.5e+3", 1, AMIAPI_JSON_NUMBER },
        { "\"hello\"", 1, AMIAPI_JSON_STRING },
        { "[]", 1, AMIAPI_JSON_ARRAY },
        { "{}", 1, AMIAPI_JSON_OBJECT },
        { "[1,2,3]", 1, AMIAPI_JSON_ARRAY },
        {
            "{\"name\":\"AmiAPI\",\"ok\":true}",
            1,
            AMIAPI_JSON_OBJECT
        },

        { "", 0, AMIAPI_JSON_NULL },
        { "tru", 0, AMIAPI_JSON_NULL },
        { "01", 0, AMIAPI_JSON_NULL },
        { "1.", 0, AMIAPI_JSON_NULL },
        { "[1,]", 0, AMIAPI_JSON_NULL },
        { "{\"a\":1,}", 0, AMIAPI_JSON_NULL },
        { "{\"a\"}", 0, AMIAPI_JSON_NULL },
        { "{]", 0, AMIAPI_JSON_NULL },
        { "true false", 0, AMIAPI_JSON_NULL },
        { "\"bad\\x\"", 0, AMIAPI_JSON_NULL }
    };

    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        enum AmiAPIJsonError error;
        size_t offset;
        struct AmiAPIJsonDoc *doc;

        error = AMIAPI_JSON_OK;
        offset = 0;

        doc = AmiAPI_JSONParse(
            cases[i].json,
            strlen(cases[i].json),
            &error,
            &offset
        );

        if (cases[i].should_pass) {
            if (!doc) {
                fprintf(
                    stderr,
                    "FAIL valid case %lu: %s "
                    "error=%d offset=%lu\n",
                    (unsigned long)i,
                    cases[i].json,
                    (int)error,
                    (unsigned long)offset
                );
                return 1;
            }

            if (AmiAPI_JSONType(
                    AmiAPI_JSONRoot(doc)) !=
                cases[i].root_type)
            {
                fprintf(
                    stderr,
                    "FAIL root type case %lu\n",
                    (unsigned long)i
                );

                AmiAPI_JSONFree(doc);
                return 1;
            }

            AmiAPI_JSONFree(doc);
        }
        else {
            if (doc) {
                fprintf(
                    stderr,
                    "FAIL invalid case accepted %lu: %s\n",
                    (unsigned long)i,
                    cases[i].json
                );

                AmiAPI_JSONFree(doc);
                return 1;
            }

            if (error == AMIAPI_JSON_OK) {
                fprintf(
                    stderr,
                    "FAIL invalid case had no error %lu\n",
                    (unsigned long)i
                );
                return 1;
            }
        }
    }

    if (!test_unicode())
        return 1;

    if (!test_depth())
        return 1;

    if (!test_query_api())
        return 1;

    puts("JSON parser basic tests: OK");
    puts("JSON Unicode tests: OK");
    puts("JSON depth tests: OK");
    puts("JSON query/value tests: OK");

    return 0;
}
