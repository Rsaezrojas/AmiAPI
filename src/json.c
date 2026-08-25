#include <limits.h>
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

static int json_value_belongs_to_doc(
    const struct AmiAPIJsonDoc *doc,
    const struct AmiAPIJsonValue *value)
{
    if (!doc || !value || !doc->values || doc->value_count == 0)
        return 0;

    return value >= doc->values &&
           value < doc->values + doc->value_count;
}

static const struct AmiAPIJsonValue *json_child_at(
    const struct AmiAPIJsonDoc *doc,
    const struct AmiAPIJsonValue *parent,
    size_t index)
{
    size_t child;

    if (!json_value_belongs_to_doc(doc, parent))
        return NULL;

    if (index >= parent->child_count)
        return NULL;

    child = parent->first_child;

    while (index > 0) {
        if (child == JSON_NO_NODE ||
            child >= doc->value_count)
        {
            return NULL;
        }

        child = doc->values[child].next_sibling;
        --index;
    }

    if (child == JSON_NO_NODE ||
        child >= doc->value_count)
    {
        return NULL;
    }

    return &doc->values[child];
}

static int json_decode_hex4_at(
    const unsigned char *data,
    size_t size,
    size_t *pos,
    uint32_t *value)
{
    uint32_t result;
    int digit;
    unsigned int i;

    result = 0;

    for (i = 0; i < 4; ++i) {
        if (*pos >= size)
            return 0;

        digit = hex_digit_value(data[*pos]);

        if (digit < 0)
            return 0;

        result = (result << 4) | (uint32_t)digit;
        (*pos)++;
    }

    *value = result;
    return 1;
}

static size_t utf8_encoded_size(uint32_t cp)
{
    if (cp <= 0x7fUL)
        return 1;

    if (cp <= 0x7ffUL)
        return 2;

    if (cp <= 0xffffUL)
        return 3;

    return 4;
}

static void utf8_write(
    char *buffer,
    size_t *used,
    uint32_t cp)
{
    if (cp <= 0x7fUL) {
        buffer[(*used)++] = (char)cp;
    }
    else if (cp <= 0x7ffUL) {
        buffer[(*used)++] =
            (char)(0xc0U | ((cp >> 6) & 0x1fU));

        buffer[(*used)++] =
            (char)(0x80U | (cp & 0x3fU));
    }
    else if (cp <= 0xffffUL) {
        buffer[(*used)++] =
            (char)(0xe0U | ((cp >> 12) & 0x0fU));

        buffer[(*used)++] =
            (char)(0x80U | ((cp >> 6) & 0x3fU));

        buffer[(*used)++] =
            (char)(0x80U | (cp & 0x3fU));
    }
    else {
        buffer[(*used)++] =
            (char)(0xf0U | ((cp >> 18) & 0x07U));

        buffer[(*used)++] =
            (char)(0x80U | ((cp >> 12) & 0x3fU));

        buffer[(*used)++] =
            (char)(0x80U | ((cp >> 6) & 0x3fU));

        buffer[(*used)++] =
            (char)(0x80U | (cp & 0x3fU));
    }
}

/*
 * Decode one already-validated JSON string token.
 *
 * If buffer is NULL, only the decoded size is calculated.
 */
static enum AmiAPIJsonError json_decode_string(
    const unsigned char *data,
    size_t size,
    char *buffer,
    size_t buffer_size,
    size_t *required_size)
{
    size_t pos;
    size_t used;
    unsigned char c;
    uint32_t cp;
    uint32_t low;
    size_t need;

    if (!data || !required_size)
        return AMIAPI_JSON_ERR_INVALID_ARGUMENT;

    pos = 0;
    used = 0;

    while (pos < size) {
        c = data[pos++];

        if (c != '\\') {
            need = 1;

            if (buffer && used + need < buffer_size)
                buffer[used] = (char)c;

            used += need;
            continue;
        }

        if (pos >= size)
            return AMIAPI_JSON_ERR_SYNTAX;

        c = data[pos++];

        switch (c) {
        case '"':  cp = '"';  break;
        case '\\': cp = '\\'; break;
        case '/':  cp = '/';  break;
        case 'b':  cp = '\b'; break;
        case 'f':  cp = '\f'; break;
        case 'n':  cp = '\n'; break;
        case 'r':  cp = '\r'; break;
        case 't':  cp = '\t'; break;

        case 'u':
            if (!json_decode_hex4_at(
                    data, size, &pos, &cp))
            {
                return AMIAPI_JSON_ERR_SYNTAX;
            }

            if (cp >= 0xd800UL &&
                cp <= 0xdbffUL)
            {
                if (size - pos < 6 ||
                    data[pos] != '\\' ||
                    data[pos + 1] != 'u')
                {
                    return AMIAPI_JSON_ERR_UTF8;
                }

                pos += 2;

                if (!json_decode_hex4_at(
                        data, size, &pos, &low))
                {
                    return AMIAPI_JSON_ERR_SYNTAX;
                }

                if (low < 0xdc00UL ||
                    low > 0xdfffUL)
                {
                    return AMIAPI_JSON_ERR_UTF8;
                }

                cp =
                    0x10000UL +
                    ((cp - 0xd800UL) << 10) +
                    (low - 0xdc00UL);
            }

            need = utf8_encoded_size(cp);

            if (buffer &&
                used + need < buffer_size)
            {
                utf8_write(buffer, &used, cp);
            }
            else {
                used += need;
            }

            continue;

        default:
            return AMIAPI_JSON_ERR_SYNTAX;
        }

        if (buffer && used + 1 < buffer_size)
            buffer[used] = (char)cp;

        used++;
    }

    /*
     * required_size includes terminating NUL.
     */
    if (used == (size_t)-1)
        return AMIAPI_JSON_ERR_NO_MEMORY;

    *required_size = used + 1;

    if (buffer && buffer_size > 0) {
        if (used < buffer_size)
            buffer[used] = '\0';
        else
            buffer[buffer_size - 1] = '\0';
    }

    if (buffer && buffer_size < used + 1)
        return AMIAPI_JSON_ERR_NO_MEMORY;

    return AMIAPI_JSON_OK;
}

static int json_key_equals(
    const struct AmiAPIJsonDoc *doc,
    const struct AmiAPIJsonValue *value,
    const char *key)
{
    size_t required;
    size_t key_len;
    char *decoded;
    enum AmiAPIJsonError err;
    int equal;

    if (!doc || !value || !key ||
        value->key_start == JSON_NO_NODE)
    {
        return 0;
    }

    key_len = strlen(key);

    /*
     * Fast path: no escape in the stored key.
     */
    if (!memchr(
            doc->data + value->key_start,
            '\\',
            value->key_length))
    {
        return value->key_length == key_len &&
               memcmp(
                   doc->data + value->key_start,
                   key,
                   key_len) == 0;
    }

    required = 0;

    err = json_decode_string(
        doc->data + value->key_start,
        value->key_length,
        NULL,
        0,
        &required
    );

    if (err != AMIAPI_JSON_OK || required == 0)
        return 0;

    decoded = (char *)malloc(required);
    if (!decoded)
        return 0;

    err = json_decode_string(
        doc->data + value->key_start,
        value->key_length,
        decoded,
        required,
        &required
    );

    equal =
        err == AMIAPI_JSON_OK &&
        strcmp(decoded, key) == 0;

    free(decoded);
    return equal;
}

const struct AmiAPIJsonValue *AmiAPI_JSONObjectGet(
    const struct AmiAPIJsonDoc *doc,
    const struct AmiAPIJsonValue *object,
    const char *key)
{
    size_t child;

    if (!json_value_belongs_to_doc(doc, object) ||
        object->type != AMIAPI_JSON_OBJECT ||
        !key)
    {
        return NULL;
    }

    child = object->first_child;

    while (child != JSON_NO_NODE) {
        if (child >= doc->value_count)
            return NULL;

        if (json_key_equals(
                doc,
                &doc->values[child],
                key))
        {
            return &doc->values[child];
        }

        child = doc->values[child].next_sibling;
    }

    return NULL;
}

size_t AmiAPI_JSONArraySize(
    const struct AmiAPIJsonValue *array)
{
    if (!array ||
        array->type != AMIAPI_JSON_ARRAY)
    {
        return 0;
    }

    return array->child_count;
}

const struct AmiAPIJsonValue *AmiAPI_JSONArrayGet(
    const struct AmiAPIJsonDoc *doc,
    const struct AmiAPIJsonValue *array,
    size_t index)
{
    if (!json_value_belongs_to_doc(doc, array) ||
        array->type != AMIAPI_JSON_ARRAY)
    {
        return NULL;
    }

    return json_child_at(doc, array, index);
}

int AmiAPI_JSONBool(
    const struct AmiAPIJsonValue *value,
    int *result)
{
    if (!value ||
        value->type != AMIAPI_JSON_BOOL ||
        !result)
    {
        return 0;
    }

    *result = value->length == 4 ? 1 : 0;
    return 1;
}

enum AmiAPIJsonError AmiAPI_JSONStringCopy(
    const struct AmiAPIJsonDoc *doc,
    const struct AmiAPIJsonValue *value,
    char *buffer,
    size_t buffer_size,
    size_t *required_size)
{
    if (!json_value_belongs_to_doc(doc, value) ||
        value->type != AMIAPI_JSON_STRING ||
        !required_size ||
        (!buffer && buffer_size != 0))
    {
        return AMIAPI_JSON_ERR_INVALID_ARGUMENT;
    }

    return json_decode_string(
        doc->data + value->start,
        value->length,
        buffer,
        buffer_size,
        required_size
    );
}

enum AmiAPIJsonError AmiAPI_JSONNumberToken(
    const struct AmiAPIJsonDoc *doc,
    const struct AmiAPIJsonValue *value,
    const char **text,
    size_t *length)
{
    if (!json_value_belongs_to_doc(doc, value) ||
        value->type != AMIAPI_JSON_NUMBER ||
        !text ||
        !length)
    {
        return AMIAPI_JSON_ERR_INVALID_ARGUMENT;
    }

    *text = (const char *)(
        doc->data + value->start
    );

    *length = value->length;

    return AMIAPI_JSON_OK;
}

enum AmiAPIJsonError AmiAPI_JSONULong(
    const struct AmiAPIJsonDoc *doc,
    const struct AmiAPIJsonValue *value,
    unsigned long *result)
{
    const char *text;
    size_t len;
    size_t i;
    unsigned long n;
    unsigned int digit;

    if (!result)
        return AMIAPI_JSON_ERR_INVALID_ARGUMENT;

    if (AmiAPI_JSONNumberToken(
            doc,
            value,
            &text,
            &len) != AMIAPI_JSON_OK)
    {
        return AMIAPI_JSON_ERR_INVALID_ARGUMENT;
    }

    if (len == 0 || text[0] == '-')
        return AMIAPI_JSON_ERR_SYNTAX;

    n = 0;

    for (i = 0; i < len; ++i) {
        if (text[i] < '0' || text[i] > '9')
            return AMIAPI_JSON_ERR_SYNTAX;

        digit = (unsigned int)(text[i] - '0');

        if (n >
            (ULONG_MAX - digit) / 10UL)
        {
            return AMIAPI_JSON_ERR_SYNTAX;
        }

        n = n * 10UL + digit;
    }

    *result = n;
    return AMIAPI_JSON_OK;
}

enum AmiAPIJsonError AmiAPI_JSONLong(
    const struct AmiAPIJsonDoc *doc,
    const struct AmiAPIJsonValue *value,
    long *result)
{
    const char *text;
    size_t len;
    size_t i;
    int negative;
    unsigned long n;
    unsigned long limit;
    unsigned int digit;

    if (!result)
        return AMIAPI_JSON_ERR_INVALID_ARGUMENT;

    if (AmiAPI_JSONNumberToken(
            doc,
            value,
            &text,
            &len) != AMIAPI_JSON_OK)
    {
        return AMIAPI_JSON_ERR_INVALID_ARGUMENT;
    }

    if (len == 0)
        return AMIAPI_JSON_ERR_SYNTAX;

    negative = text[0] == '-';
    i = negative ? 1 : 0;

    if (i == len)
        return AMIAPI_JSON_ERR_SYNTAX;

    limit = negative
        ? (unsigned long)LONG_MAX + 1UL
        : (unsigned long)LONG_MAX;

    n = 0;

    for (; i < len; ++i) {
        if (text[i] < '0' || text[i] > '9')
            return AMIAPI_JSON_ERR_SYNTAX;

        digit = (unsigned int)(text[i] - '0');

        if (n > (limit - digit) / 10UL)
            return AMIAPI_JSON_ERR_SYNTAX;

        n = n * 10UL + digit;
    }

    if (negative) {
        if (n == (unsigned long)LONG_MAX + 1UL)
            *result = LONG_MIN;
        else
            *result = -(long)n;
    }
    else {
        *result = (long)n;
    }

    return AMIAPI_JSON_OK;
}
