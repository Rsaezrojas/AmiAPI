#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <amiapi/json.h>

#define JSON_NO_NODE ((size_t)-1)

struct AmiAPIJsonValue {
    enum AmiAPIJsonType type;

    size_t start;
    size_t length;

    size_t key_start;
    size_t key_length;

    size_t first_child;
    size_t next_sibling;
    size_t child_count;
};

struct AmiAPIJsonDoc {
    unsigned char *data;
    size_t size;

    struct AmiAPIJsonValue *values;
    size_t value_count;
    size_t value_capacity;
};

struct JsonParser {
    struct AmiAPIJsonDoc *doc;

    size_t pos;
    unsigned int depth;

    enum AmiAPIJsonError error;
    size_t error_offset;
};

static void parser_fail(
    struct JsonParser *parser,
    enum AmiAPIJsonError error)
{
    if (parser->error != AMIAPI_JSON_OK)
        return;

    parser->error = error;
    parser->error_offset = parser->pos;
}

static int is_json_whitespace(unsigned char c)
{
    return c == ' ' ||
           c == '\t' ||
           c == '\r' ||
           c == '\n';
}

static void skip_whitespace(struct JsonParser *parser)
{
    while (parser->pos < parser->doc->size &&
           is_json_whitespace(parser->doc->data[parser->pos]))
    {
        parser->pos++;
    }
}

static int parser_match(
    struct JsonParser *parser,
    const char *literal)
{
    size_t len;

    len = strlen(literal);

    if (len > parser->doc->size - parser->pos)
        return 0;

    if (memcmp(
            parser->doc->data + parser->pos,
            literal,
            len) != 0)
    {
        return 0;
    }

    parser->pos += len;
    return 1;
}

static size_t add_value(
    struct JsonParser *parser,
    enum AmiAPIJsonType type,
    size_t start)
{
    struct AmiAPIJsonValue *values;
    size_t new_capacity;
    size_t index;

    if (parser->doc->value_count == parser->doc->value_capacity) {
        if (parser->doc->value_capacity == 0)
            new_capacity = 16;
        else {
            if (parser->doc->value_capacity >
                ((size_t)-1) / 2)
            {
                parser_fail(
                    parser,
                    AMIAPI_JSON_ERR_NO_MEMORY
                );
                return JSON_NO_NODE;
            }

            new_capacity =
                parser->doc->value_capacity * 2;
        }

        if (new_capacity >
            ((size_t)-1) /
            sizeof(*parser->doc->values))
        {
            parser_fail(
                parser,
                AMIAPI_JSON_ERR_NO_MEMORY
            );
            return JSON_NO_NODE;
        }

        values = (struct AmiAPIJsonValue *)realloc(
            parser->doc->values,
            new_capacity * sizeof(*parser->doc->values)
        );

        if (!values) {
            parser_fail(
                parser,
                AMIAPI_JSON_ERR_NO_MEMORY
            );
            return JSON_NO_NODE;
        }

        parser->doc->values = values;
        parser->doc->value_capacity = new_capacity;
    }

    index = parser->doc->value_count++;

    memset(
        &parser->doc->values[index],
        0,
        sizeof(parser->doc->values[index])
    );

    parser->doc->values[index].type = type;
    parser->doc->values[index].start = start;
    parser->doc->values[index].key_start = JSON_NO_NODE;
    parser->doc->values[index].first_child = JSON_NO_NODE;
    parser->doc->values[index].next_sibling = JSON_NO_NODE;

    return index;
}

static void append_child(
    struct AmiAPIJsonDoc *doc,
    size_t parent,
    size_t child)
{
    size_t current;

    if (doc->values[parent].first_child == JSON_NO_NODE) {
        doc->values[parent].first_child = child;
    }
    else {
        current = doc->values[parent].first_child;

        while (doc->values[current].next_sibling != JSON_NO_NODE)
            current = doc->values[current].next_sibling;

        doc->values[current].next_sibling = child;
    }

    doc->values[parent].child_count++;
}

static int hex_digit_value(unsigned char c)
{
    if (c >= '0' && c <= '9')
        return (int)(c - '0');

    if (c >= 'a' && c <= 'f')
        return (int)(c - 'a') + 10;

    if (c >= 'A' && c <= 'F')
        return (int)(c - 'A') + 10;

    return -1;
}

static int parse_hex4(
    struct JsonParser *parser,
    uint32_t *value)
{
    uint32_t result;
    int digit;
    unsigned int i;

    result = 0;

    for (i = 0; i < 4; ++i) {
        if (parser->pos >= parser->doc->size) {
            parser_fail(
                parser,
                AMIAPI_JSON_ERR_SYNTAX
            );
            return 0;
        }

        digit = hex_digit_value(
            parser->doc->data[parser->pos]
        );

        if (digit < 0) {
            parser_fail(
                parser,
                AMIAPI_JSON_ERR_SYNTAX
            );
            return 0;
        }

        result =
            (result << 4) |
            (uint32_t)digit;

        parser->pos++;
    }

    *value = result;
    return 1;
}

static int is_utf8_continuation(unsigned char c)
{
    return (c & 0xc0U) == 0x80U;
}

static int consume_raw_utf8(
    struct JsonParser *parser,
    unsigned char first)
{
    unsigned char b1;
    unsigned char b2;
    unsigned char b3;

    /*
     * first has already been consumed by parse_string_token().
     */

    if (first >= 0xc2U && first <= 0xdfU) {
        if (parser->pos >= parser->doc->size)
            goto invalid;

        b1 = parser->doc->data[parser->pos];

        if (!is_utf8_continuation(b1))
            goto invalid;

        parser->pos += 1;
        return 1;
    }

    if (first >= 0xe0U && first <= 0xefU) {
        if (parser->doc->size - parser->pos < 2)
            goto invalid;

        b1 = parser->doc->data[parser->pos];
        b2 = parser->doc->data[parser->pos + 1];

        if (!is_utf8_continuation(b2))
            goto invalid;

        /*
         * E0 must not encode an overlong sequence.
         */
        if (first == 0xe0U) {
            if (b1 < 0xa0U || b1 > 0xbfU)
                goto invalid;
        }
        /*
         * ED A0..BF would encode UTF-16 surrogate code points,
         * which are not valid Unicode scalar values in UTF-8.
         */
        else if (first == 0xedU) {
            if (b1 < 0x80U || b1 > 0x9fU)
                goto invalid;
        }
        else {
            if (!is_utf8_continuation(b1))
                goto invalid;
        }

        parser->pos += 2;
        return 1;
    }

    if (first >= 0xf0U && first <= 0xf4U) {
        if (parser->doc->size - parser->pos < 3)
            goto invalid;

        b1 = parser->doc->data[parser->pos];
        b2 = parser->doc->data[parser->pos + 1];
        b3 = parser->doc->data[parser->pos + 2];

        if (!is_utf8_continuation(b2) ||
            !is_utf8_continuation(b3))
        {
            goto invalid;
        }

        /*
         * F0 80..8F would be overlong.
         */
        if (first == 0xf0U) {
            if (b1 < 0x90U || b1 > 0xbfU)
                goto invalid;
        }
        /*
         * Unicode ends at U+10FFFF.
         */
        else if (first == 0xf4U) {
            if (b1 < 0x80U || b1 > 0x8fU)
                goto invalid;
        }
        else {
            if (!is_utf8_continuation(b1))
                goto invalid;
        }

        parser->pos += 3;
        return 1;
    }

invalid:
    parser_fail(
        parser,
        AMIAPI_JSON_ERR_UTF8
    );
    return 0;
}

static int parse_unicode_escape(
    struct JsonParser *parser)
{
    uint32_t first;
    uint32_t second;

    if (!parse_hex4(parser, &first))
        return 0;

    /*
     * High surrogate: JSON must contain an immediately following
     * low-surrogate escape.
     */
    if (first >= 0xd800UL &&
        first <= 0xdbffUL)
    {
        if (parser->doc->size - parser->pos < 6 ||
            parser->doc->data[parser->pos] != '\\' ||
            parser->doc->data[parser->pos + 1] != 'u')
        {
            parser_fail(
                parser,
                AMIAPI_JSON_ERR_UTF8
            );
            return 0;
        }

        parser->pos += 2;

        if (!parse_hex4(parser, &second))
            return 0;

        if (second < 0xdc00UL ||
            second > 0xdfffUL)
        {
            parser_fail(
                parser,
                AMIAPI_JSON_ERR_UTF8
            );
            return 0;
        }

        return 1;
    }

    /*
     * A low surrogate without a preceding high surrogate is invalid.
     */
    if (first >= 0xdc00UL &&
        first <= 0xdfffUL)
    {
        parser_fail(
            parser,
            AMIAPI_JSON_ERR_UTF8
        );
        return 0;
    }

    return 1;
}

static int parse_string_token(
    struct JsonParser *parser,
    size_t *content_start,
    size_t *content_length)
{
    size_t start;
    unsigned char c;

    if (parser->pos >= parser->doc->size ||
        parser->doc->data[parser->pos] != '"')
    {
        parser_fail(
            parser,
            AMIAPI_JSON_ERR_SYNTAX
        );
        return 0;
    }

    parser->pos++;
    start = parser->pos;

    while (parser->pos < parser->doc->size) {
        c = parser->doc->data[parser->pos++];

        if (c == '"') {
            *content_start = start;
            *content_length =
                (parser->pos - 1) - start;
            return 1;
        }

        /*
         * JSON strings may not contain unescaped C0 controls,
         * including an embedded NUL byte.
         */
        if (c < 0x20U) {
            parser_fail(
                parser,
                AMIAPI_JSON_ERR_SYNTAX
            );
            return 0;
        }

        if (c >= 0x80U) {
            if (!consume_raw_utf8(parser, c))
                return 0;

            continue;
        }

        if (c != '\\')
            continue;

        if (parser->pos >= parser->doc->size) {
            parser_fail(
                parser,
                AMIAPI_JSON_ERR_SYNTAX
            );
            return 0;
        }

        c = parser->doc->data[parser->pos++];

        switch (c) {
        case '"':
        case '\\':
        case '/':
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
            break;

        case 'u':
            if (!parse_unicode_escape(parser))
                return 0;
            break;

        default:
            parser_fail(
                parser,
                AMIAPI_JSON_ERR_SYNTAX
            );
            return 0;
        }
    }

    parser_fail(
        parser,
        AMIAPI_JSON_ERR_SYNTAX
    );
    return 0;
}

static int parse_number_token(
    struct JsonParser *parser,
    size_t *start_out,
    size_t *length_out)
{
    size_t start;
    unsigned char c;

    start = parser->pos;

    if (parser->pos < parser->doc->size &&
        parser->doc->data[parser->pos] == '-')
    {
        parser->pos++;
    }

    if (parser->pos >= parser->doc->size) {
        parser_fail(parser, AMIAPI_JSON_ERR_SYNTAX);
        return 0;
    }

    c = parser->doc->data[parser->pos];

    if (c == '0') {
        parser->pos++;

        if (parser->pos < parser->doc->size) {
            c = parser->doc->data[parser->pos];

            if (c >= '0' && c <= '9') {
                parser_fail(
                    parser,
                    AMIAPI_JSON_ERR_SYNTAX
                );
                return 0;
            }
        }
    }
    else if (c >= '1' && c <= '9') {
        do {
            parser->pos++;

            if (parser->pos >= parser->doc->size)
                break;

            c = parser->doc->data[parser->pos];
        } while (c >= '0' && c <= '9');
    }
    else {
        parser_fail(parser, AMIAPI_JSON_ERR_SYNTAX);
        return 0;
    }

    if (parser->pos < parser->doc->size &&
        parser->doc->data[parser->pos] == '.')
    {
        parser->pos++;

        if (parser->pos >= parser->doc->size ||
            parser->doc->data[parser->pos] < '0' ||
            parser->doc->data[parser->pos] > '9')
        {
            parser_fail(parser, AMIAPI_JSON_ERR_SYNTAX);
            return 0;
        }

        while (parser->pos < parser->doc->size) {
            c = parser->doc->data[parser->pos];

            if (c < '0' || c > '9')
                break;

            parser->pos++;
        }
    }

    if (parser->pos < parser->doc->size &&
        (parser->doc->data[parser->pos] == 'e' ||
         parser->doc->data[parser->pos] == 'E'))
    {
        parser->pos++;

        if (parser->pos < parser->doc->size &&
            (parser->doc->data[parser->pos] == '+' ||
             parser->doc->data[parser->pos] == '-'))
        {
            parser->pos++;
        }

        if (parser->pos >= parser->doc->size ||
            parser->doc->data[parser->pos] < '0' ||
            parser->doc->data[parser->pos] > '9')
        {
            parser_fail(parser, AMIAPI_JSON_ERR_SYNTAX);
            return 0;
        }

        while (parser->pos < parser->doc->size) {
            c = parser->doc->data[parser->pos];

            if (c < '0' || c > '9')
                break;

            parser->pos++;
        }
    }

    *start_out = start;
    *length_out = parser->pos - start;

    return 1;
}

static size_t parse_value(struct JsonParser *parser);

static size_t parse_array(struct JsonParser *parser)
{
    size_t node;
    size_t child;
    size_t start;

    start = parser->pos;

    if (parser->depth >= AMIAPI_JSON_DEFAULT_MAX_DEPTH) {
        parser_fail(parser, AMIAPI_JSON_ERR_DEPTH);
        return JSON_NO_NODE;
    }

    node = add_value(
        parser,
        AMIAPI_JSON_ARRAY,
        start
    );

    if (node == JSON_NO_NODE)
        return JSON_NO_NODE;

    parser->depth++;
    parser->pos++;
    skip_whitespace(parser);

    if (parser->pos < parser->doc->size &&
        parser->doc->data[parser->pos] == ']')
    {
        parser->pos++;
        parser->depth--;
        parser->doc->values[node].length =
            parser->pos - start;
        return node;
    }

    for (;;) {
        child = parse_value(parser);

        if (child == JSON_NO_NODE) {
            parser->depth--;
            return JSON_NO_NODE;
        }

        append_child(parser->doc, node, child);

        skip_whitespace(parser);

        if (parser->pos >= parser->doc->size) {
            parser_fail(parser, AMIAPI_JSON_ERR_SYNTAX);
            parser->depth--;
            return JSON_NO_NODE;
        }

        if (parser->doc->data[parser->pos] == ']') {
            parser->pos++;
            parser->depth--;
            parser->doc->values[node].length =
                parser->pos - start;
            return node;
        }

        if (parser->doc->data[parser->pos] != ',') {
            parser_fail(parser, AMIAPI_JSON_ERR_SYNTAX);
            parser->depth--;
            return JSON_NO_NODE;
        }

        parser->pos++;
        skip_whitespace(parser);

        if (parser->pos < parser->doc->size &&
            parser->doc->data[parser->pos] == ']')
        {
            parser_fail(parser, AMIAPI_JSON_ERR_SYNTAX);
            parser->depth--;
            return JSON_NO_NODE;
        }
    }
}

static size_t parse_object(struct JsonParser *parser)
{
    size_t node;
    size_t child;
    size_t key_start;
    size_t key_length;
    size_t start;

    start = parser->pos;

    if (parser->depth >= AMIAPI_JSON_DEFAULT_MAX_DEPTH) {
        parser_fail(parser, AMIAPI_JSON_ERR_DEPTH);
        return JSON_NO_NODE;
    }

    node = add_value(
        parser,
        AMIAPI_JSON_OBJECT,
        start
    );

    if (node == JSON_NO_NODE)
        return JSON_NO_NODE;

    parser->depth++;
    parser->pos++;
    skip_whitespace(parser);

    if (parser->pos < parser->doc->size &&
        parser->doc->data[parser->pos] == '}')
    {
        parser->pos++;
        parser->depth--;
        parser->doc->values[node].length =
            parser->pos - start;
        return node;
    }

    for (;;) {
        if (!parse_string_token(
                parser,
                &key_start,
                &key_length))
        {
            parser->depth--;
            return JSON_NO_NODE;
        }

        skip_whitespace(parser);

        if (parser->pos >= parser->doc->size ||
            parser->doc->data[parser->pos] != ':')
        {
            parser_fail(parser, AMIAPI_JSON_ERR_SYNTAX);
            parser->depth--;
            return JSON_NO_NODE;
        }

        parser->pos++;
        skip_whitespace(parser);

        child = parse_value(parser);

        if (child == JSON_NO_NODE) {
            parser->depth--;
            return JSON_NO_NODE;
        }

        parser->doc->values[child].key_start = key_start;
        parser->doc->values[child].key_length = key_length;

        append_child(parser->doc, node, child);

        skip_whitespace(parser);

        if (parser->pos >= parser->doc->size) {
            parser_fail(parser, AMIAPI_JSON_ERR_SYNTAX);
            parser->depth--;
            return JSON_NO_NODE;
        }

        if (parser->doc->data[parser->pos] == '}') {
            parser->pos++;
            parser->depth--;
            parser->doc->values[node].length =
                parser->pos - start;
            return node;
        }

        if (parser->doc->data[parser->pos] != ',') {
            parser_fail(parser, AMIAPI_JSON_ERR_SYNTAX);
            parser->depth--;
            return JSON_NO_NODE;
        }

        parser->pos++;
        skip_whitespace(parser);

        if (parser->pos < parser->doc->size &&
            parser->doc->data[parser->pos] == '}')
        {
            parser_fail(parser, AMIAPI_JSON_ERR_SYNTAX);
            parser->depth--;
            return JSON_NO_NODE;
        }
    }
}

static size_t parse_value(struct JsonParser *parser)
{
    size_t node;
    size_t start;
    size_t token_start;
    size_t token_length;
    unsigned char c;

    skip_whitespace(parser);

    if (parser->pos >= parser->doc->size) {
        parser_fail(parser, AMIAPI_JSON_ERR_SYNTAX);
        return JSON_NO_NODE;
    }

    start = parser->pos;
    c = parser->doc->data[parser->pos];

    if (c == '{')
        return parse_object(parser);

    if (c == '[')
        return parse_array(parser);

    if (c == '"') {
        if (!parse_string_token(
                parser,
                &token_start,
                &token_length))
        {
            return JSON_NO_NODE;
        }

        node = add_value(
            parser,
            AMIAPI_JSON_STRING,
            token_start
        );

        if (node == JSON_NO_NODE)
            return JSON_NO_NODE;

        parser->doc->values[node].length = token_length;
        return node;
    }

    if (c == 'n') {
        if (!parser_match(parser, "null")) {
            parser_fail(parser, AMIAPI_JSON_ERR_SYNTAX);
            return JSON_NO_NODE;
        }

        node = add_value(
            parser,
            AMIAPI_JSON_NULL,
            start
        );

        if (node != JSON_NO_NODE)
            parser->doc->values[node].length = 4;

        return node;
    }

    if (c == 't') {
        if (!parser_match(parser, "true")) {
            parser_fail(parser, AMIAPI_JSON_ERR_SYNTAX);
            return JSON_NO_NODE;
        }

        node = add_value(
            parser,
            AMIAPI_JSON_BOOL,
            start
        );

        if (node != JSON_NO_NODE)
            parser->doc->values[node].length = 4;

        return node;
    }

    if (c == 'f') {
        if (!parser_match(parser, "false")) {
            parser_fail(parser, AMIAPI_JSON_ERR_SYNTAX);
            return JSON_NO_NODE;
        }

        node = add_value(
            parser,
            AMIAPI_JSON_BOOL,
            start
        );

        if (node != JSON_NO_NODE)
            parser->doc->values[node].length = 5;

        return node;
    }

    if (c == '-' || (c >= '0' && c <= '9')) {
        if (!parse_number_token(
                parser,
                &token_start,
                &token_length))
        {
            return JSON_NO_NODE;
        }

        node = add_value(
            parser,
            AMIAPI_JSON_NUMBER,
            token_start
        );

        if (node == JSON_NO_NODE)
            return JSON_NO_NODE;

        parser->doc->values[node].length = token_length;
        return node;
    }

    parser_fail(parser, AMIAPI_JSON_ERR_SYNTAX);
    return JSON_NO_NODE;
}

struct AmiAPIJsonDoc *AmiAPI_JSONParse(
    const void *data,
    size_t size,
    enum AmiAPIJsonError *error,
    size_t *error_offset)
{
    struct AmiAPIJsonDoc *doc;
    struct JsonParser parser;
    size_t root;

    if (error)
        *error = AMIAPI_JSON_OK;

    if (error_offset)
        *error_offset = 0;

    if (!data && size != 0) {
        if (error)
            *error = AMIAPI_JSON_ERR_INVALID_ARGUMENT;

        return NULL;
    }

    doc = (struct AmiAPIJsonDoc *)calloc(
        1,
        sizeof(*doc)
    );

    if (!doc) {
        if (error)
            *error = AMIAPI_JSON_ERR_NO_MEMORY;

        return NULL;
    }

    if (size != 0) {
        doc->data = (unsigned char *)malloc(size);

        if (!doc->data) {
            free(doc);

            if (error)
                *error = AMIAPI_JSON_ERR_NO_MEMORY;

            return NULL;
        }

        memcpy(doc->data, data, size);
    }

    doc->size = size;

    memset(&parser, 0, sizeof(parser));
    parser.doc = doc;
    parser.error = AMIAPI_JSON_OK;

    skip_whitespace(&parser);

    root = parse_value(&parser);

    if (root == JSON_NO_NODE)
        goto fail;

    skip_whitespace(&parser);

    if (parser.pos != doc->size) {
        parser_fail(
            &parser,
            AMIAPI_JSON_ERR_SYNTAX
        );
        goto fail;
    }

    if (doc->value_count == 0 || root != 0) {
        parser_fail(
            &parser,
            AMIAPI_JSON_ERR_SYNTAX
        );
        goto fail;
    }

    return doc;

fail:
    if (error)
        *error = parser.error;

    if (error_offset)
        *error_offset = parser.error_offset;

    AmiAPI_JSONFree(doc);
    return NULL;
}

void AmiAPI_JSONFree(struct AmiAPIJsonDoc *doc)
{
    if (!doc)
        return;

    free(doc->values);
    free(doc->data);
    free(doc);
}

const struct AmiAPIJsonValue *AmiAPI_JSONRoot(
    const struct AmiAPIJsonDoc *doc)
{
    if (!doc || doc->value_count == 0)
        return NULL;

    return &doc->values[0];
}

enum AmiAPIJsonType AmiAPI_JSONType(
    const struct AmiAPIJsonValue *value)
{
    return value
        ? value->type
        : AMIAPI_JSON_NULL;
}
