#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <amiapi/amiapi.h>
#include "internal.h"

#define AMIAPI_HEADER_LIMIT 32768UL
#define AMIAPI_READ_CHUNK 4096UL
#define AMIAPI_CHUNK_LINE_LIMIT 8192UL
#define AMIAPI_MAX_REDIRECTS 8

struct AmiAPIHeader { char *name; char *value; };
struct AmiAPIResponse {
    int status;
    struct AmiAPIHeader *headers;
    size_t header_count;
    unsigned char *body;
    size_t body_size;
};
struct ParsedURL {
    char *host;
    char *path;
    unsigned short port;
    int use_tls;
};

static char *dup_range(const char *start, size_t len)
{
    char *s;
    if (len == (size_t)-1) return NULL;
    s = (char *)malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

static int ascii_equal_ci(const char *a, const char *b)
{
    unsigned char ca, cb;
    while (*a && *b) {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (tolower((int)ca) != tolower((int)cb)) return 0;
    }
    return *a == '\0' && *b == '\0';
}

static void free_parsed_url(struct ParsedURL *url)
{
    if (!url) return;
    free(url->host); free(url->path);
    url->host = NULL; url->path = NULL;
}

static enum AmiAPIError parse_http_url(
    const char *input,
    struct ParsedURL *out)
{
    const char *host_start;
    const char *authority_end;
    const char *path_start;
    const char *query_start;
    const char *fragment_start;
    const char *colon;
    const char *p;
    unsigned long port;
    size_t host_len;
    size_t scheme_len;
    size_t target_len;

    if (!input || !out)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    memset(out, 0, sizeof(*out));

    if (strncmp(input, "https://", 8) == 0) {
        scheme_len = 8;
        port = 443;
        out->use_tls = 1;
    }
    else if (strncmp(input, "http://", 7) == 0) {
        scheme_len = 7;
        port = 80;
        out->use_tls = 0;
    }
    else {
        if (strstr(input, "://"))
            return AMIAPI_ERR_UNSUPPORTED_SCHEME;

        return AMIAPI_ERR_BAD_URL;
    }

    host_start = input + scheme_len;

    if (*host_start == '\0')
        return AMIAPI_ERR_BAD_URL;

    path_start = strchr(host_start, '/');
    query_start = strchr(host_start, '?');
    fragment_start = strchr(host_start, '#');

    authority_end = input + strlen(input);

    if (path_start && path_start < authority_end)
        authority_end = path_start;

    if (query_start && query_start < authority_end)
        authority_end = query_start;

    if (fragment_start && fragment_start < authority_end)
        authority_end = fragment_start;

    if (authority_end == host_start)
        return AMIAPI_ERR_BAD_URL;

    colon = NULL;

    for (p = host_start; p < authority_end; ++p) {
        if (*p == ':')
            colon = p;
    }

    if (colon) {
        char *endptr;

        if (colon == host_start || colon + 1 == authority_end)
            return AMIAPI_ERR_BAD_URL;

        port = strtoul(colon + 1, &endptr, 10);

        if (endptr != authority_end ||
            port == 0 ||
            port > 65535)
            return AMIAPI_ERR_BAD_URL;

        authority_end = colon;
    }

    host_len = (size_t)(authority_end - host_start);

    if (host_len == 0)
        return AMIAPI_ERR_BAD_URL;

    out->host = dup_range(host_start, host_len);
    if (!out->host)
        return AMIAPI_ERR_NO_MEMORY;

    /*
     * URL fragments are client-side only and must never appear in the
     * HTTP request-target.
     */
    fragment_start = strchr(input + scheme_len, '#');

    if (path_start &&
        (!query_start || path_start < query_start) &&
        (!fragment_start || path_start < fragment_start))
    {
        const char *target_end = fragment_start
            ? fragment_start
            : input + strlen(input);

        target_len = (size_t)(target_end - path_start);

        out->path = dup_range(path_start, target_len);
    }
    else if (query_start &&
             (!fragment_start || query_start < fragment_start))
    {
        const char *target_end = fragment_start
            ? fragment_start
            : input + strlen(input);

        size_t query_len = (size_t)(target_end - query_start);

        out->path = (char *)malloc(query_len + 2);

        if (out->path) {
            out->path[0] = '/';
            memcpy(out->path + 1, query_start, query_len);
            out->path[query_len + 1] = '\0';
        }
    }
    else {
        out->path = dup_range("/", 1);
    }

    if (!out->path) {
        free_parsed_url(out);
        return AMIAPI_ERR_NO_MEMORY;
    }

    out->port = (unsigned short)port;

    return AMIAPI_OK;
}

static char *trim_left(char *s) { while (*s == ' ' || *s == '\t') ++s; return s; }
static void trim_right(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) s[--len] = '\0';
}

static enum AmiAPIError add_header(struct AmiAPIResponse *r, const char *name, const char *value)
{
    struct AmiAPIHeader *nh;
    char *n = dup_range(name, strlen(name));
    char *v = dup_range(value, strlen(value));
    if (!n || !v) { free(n); free(v); return AMIAPI_ERR_NO_MEMORY; }
    if (r->header_count == (size_t)-1) { free(n); free(v); return AMIAPI_ERR_NO_MEMORY; }
    nh = (struct AmiAPIHeader *)realloc(r->headers, (r->header_count + 1) * sizeof(*r->headers));
    if (!nh) { free(n); free(v); return AMIAPI_ERR_NO_MEMORY; }
    r->headers = nh;
    r->headers[r->header_count].name = n;
    r->headers[r->header_count].value = v;
    r->header_count++;
    return AMIAPI_OK;
}

static enum AmiAPIError parse_headers(struct AmiAPIResponse *r, char *text)
{
    char *line = text, *next, *colon;
    int status;
    next = strstr(line, "\r\n");
    if (!next) return AMIAPI_ERR_PROTOCOL;
    *next = '\0';
    if (sscanf(line, "HTTP/%*u.%*u %d", &status) != 1 || status < 100 || status > 999)
        return AMIAPI_ERR_PROTOCOL;
    r->status = status;
    line = next + 2;
    while (*line) {
        next = strstr(line, "\r\n");
        if (!next) return AMIAPI_ERR_PROTOCOL;
        *next = '\0';
        if (!*line) break;
        colon = strchr(line, ':');
        if (!colon) return AMIAPI_ERR_PROTOCOL;
        *colon = '\0'; trim_right(line); colon = trim_left(colon + 1); trim_right(colon);
        if (!*line) return AMIAPI_ERR_PROTOCOL;
        { enum AmiAPIError e = add_header(r, line, colon); if (e != AMIAPI_OK) return e; }
        line = next + 2;
    }
    return AMIAPI_OK;
}

static enum AmiAPIError append_bytes(unsigned char **buffer, size_t *used,
                                     size_t *capacity, const void *data,
                                     size_t amount, size_t limit)
{
    size_t needed, wanted, nc;
    unsigned char *nb;
    if (!buffer || !used || !capacity || (!data && amount)) return AMIAPI_ERR_INVALID_ARGUMENT;
    if (*used > limit || amount > limit - *used) return AMIAPI_ERR_RESPONSE_TOO_LARGE;
    needed = *used + amount;
    if (needed == (size_t)-1) return AMIAPI_ERR_RESPONSE_TOO_LARGE;
    wanted = needed + 1;
    if (wanted > *capacity) {
        nc = *capacity ? *capacity : (AMIAPI_READ_CHUNK + 1);
        while (nc < wanted) {
            if (nc > ((size_t)-1)/2) { nc = wanted; break; }
            nc *= 2;
        }
        nb = (unsigned char *)realloc(*buffer, nc);
        if (!nb) return AMIAPI_ERR_NO_MEMORY;
        *buffer = nb; *capacity = nc;
    }
    if (amount) memcpy(*buffer + *used, data, amount);
    *used = needed;
    (*buffer)[*used] = '\0';
    return AMIAPI_OK;
}

static enum AmiAPIError parse_content_length(
    const struct AmiAPIResponse *response,
    int *present,
    size_t *value)
{
    const char *text;
    size_t result;
    size_t max_value;

    if (!response || !present || !value)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    *present = 0;
    *value = 0;

    text = AmiAPI_Header(response, "Content-Length");
    if (!text)
        return AMIAPI_OK;

    if (*text == '\0')
        return AMIAPI_ERR_PROTOCOL;

    result = 0;
    max_value = (size_t)-1;

    while (*text) {
        unsigned int digit;

        if (*text < '0' || *text > '9')
            return AMIAPI_ERR_PROTOCOL;

        digit = (unsigned int)(*text - '0');

        if (result > (max_value - digit) / 10)
            return AMIAPI_ERR_PROTOCOL;

        result = result * 10 + digit;
        ++text;
    }

    *present = 1;
    *value = result;

    return AMIAPI_OK;
}


struct AmiAPIBodyReader {
    struct AmiAPITransport *transport;

    const unsigned char *initial;
    size_t initial_size;
    size_t initial_pos;

    unsigned char *scratch;
    size_t scratch_capacity;
    size_t scratch_size;
    size_t scratch_pos;
};

static enum AmiAPIError transfer_encoding_chunked(
    const struct AmiAPIResponse *response,
    int *chunked)
{
    const char *value;

    if (!response || !chunked)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    *chunked = 0;

    value = AmiAPI_Header(response, "Transfer-Encoding");
    if (!value)
        return AMIAPI_OK;

    /*
     * M2.1 supports the HTTP/1.1 chunked transfer coding.
     * Other transfer codings are deliberately rejected until AmiAPI
     * implements them explicitly.
     */
    if (!ascii_equal_ci(value, "chunked"))
        return AMIAPI_ERR_PROTOCOL;

    *chunked = 1;
    return AMIAPI_OK;
}

static enum AmiAPIError body_reader_fill(
    struct AmiAPIBodyReader *reader)
{
    long n;

    if (!reader ||
        !reader->transport ||
        !reader->scratch ||
        reader->scratch_capacity == 0)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    n = amiapi_transport_receive(
        reader->transport,
        reader->scratch,
        reader->scratch_capacity
    );

    if (n < 0)
        return AMIAPI_ERR_RECEIVE;

    if (n == 0)
        return AMIAPI_ERR_PROTOCOL;

    reader->scratch_pos = 0;
    reader->scratch_size = (size_t)n;

    return AMIAPI_OK;
}

static enum AmiAPIError body_reader_byte(
    struct AmiAPIBodyReader *reader,
    unsigned char *value)
{
    enum AmiAPIError err;

    if (!reader || !value)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    if (reader->initial_pos < reader->initial_size) {
        *value = reader->initial[reader->initial_pos++];
        return AMIAPI_OK;
    }

    if (reader->scratch_pos >= reader->scratch_size) {
        err = body_reader_fill(reader);
        if (err != AMIAPI_OK)
            return err;
    }

    *value = reader->scratch[reader->scratch_pos++];
    return AMIAPI_OK;
}

static enum AmiAPIError body_reader_line(
    struct AmiAPIBodyReader *reader,
    char *line,
    size_t capacity,
    size_t *line_size)
{
    size_t used;
    unsigned char c;
    enum AmiAPIError err;

    if (!reader || !line || capacity < 2 || !line_size)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    used = 0;

    for (;;) {
        err = body_reader_byte(reader, &c);
        if (err != AMIAPI_OK)
            return err;

        if (c == '\n')
            return AMIAPI_ERR_PROTOCOL;

        if (c == '\r') {
            err = body_reader_byte(reader, &c);
            if (err != AMIAPI_OK)
                return err;

            if (c != '\n')
                return AMIAPI_ERR_PROTOCOL;

            line[used] = '\0';
            *line_size = used;
            return AMIAPI_OK;
        }

        if (used + 1 >= capacity)
            return AMIAPI_ERR_PROTOCOL;

        line[used++] = (char)c;
    }
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

static enum AmiAPIError parse_chunk_size(
    const char *line,
    size_t *chunk_size)
{
    const unsigned char *p;
    size_t value;
    size_t max_value;
    int digit;
    int have_digit;

    if (!line || !chunk_size)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    p = (const unsigned char *)line;
    value = 0;
    max_value = (size_t)-1;
    have_digit = 0;

    while (*p) {
        digit = hex_digit_value(*p);

        if (digit < 0)
            break;

        have_digit = 1;

        if (value > (max_value - (size_t)digit) / 16)
            return AMIAPI_ERR_PROTOCOL;

        value = value * 16 + (size_t)digit;
        ++p;
    }

    if (!have_digit)
        return AMIAPI_ERR_PROTOCOL;

    while (*p == ' ' || *p == '\t')
        ++p;

    /*
     * Ignore chunk extensions after the semicolon.
     */
    if (*p != '\0' && *p != ';')
        return AMIAPI_ERR_PROTOCOL;

    *chunk_size = value;
    return AMIAPI_OK;
}

static enum AmiAPIError body_reader_append(
    struct AmiAPIBodyReader *reader,
    struct AmiAPIResponse *response,
    size_t *body_capacity,
    size_t amount,
    size_t limit)
{
    enum AmiAPIError err;

    while (amount > 0) {
        const unsigned char *source;
        size_t available;
        size_t take;

        if (reader->initial_pos < reader->initial_size) {
            source = reader->initial + reader->initial_pos;
            available = reader->initial_size - reader->initial_pos;
            take = available < amount ? available : amount;

            err = append_bytes(
                &response->body,
                &response->body_size,
                body_capacity,
                source,
                take,
                limit
            );
            if (err != AMIAPI_OK)
                return err;

            reader->initial_pos += take;
            amount -= take;
            continue;
        }

        if (reader->scratch_pos >= reader->scratch_size) {
            err = body_reader_fill(reader);
            if (err != AMIAPI_OK)
                return err;
        }

        source = reader->scratch + reader->scratch_pos;
        available = reader->scratch_size - reader->scratch_pos;
        take = available < amount ? available : amount;

        err = append_bytes(
            &response->body,
            &response->body_size,
            body_capacity,
            source,
            take,
            limit
        );
        if (err != AMIAPI_OK)
            return err;

        reader->scratch_pos += take;
        amount -= take;
    }

    return AMIAPI_OK;
}

static enum AmiAPIError body_reader_expect_crlf(
    struct AmiAPIBodyReader *reader)
{
    unsigned char a;
    unsigned char b;
    enum AmiAPIError err;

    err = body_reader_byte(reader, &a);
    if (err != AMIAPI_OK)
        return err;

    err = body_reader_byte(reader, &b);
    if (err != AMIAPI_OK)
        return err;

    return (a == '\r' && b == '\n')
        ? AMIAPI_OK
        : AMIAPI_ERR_PROTOCOL;
}

static enum AmiAPIError decode_chunked_body(
    struct AmiAPITransport *transport,
    struct AmiAPIResponse *response,
    const unsigned char *initial,
    size_t initial_size,
    unsigned char *scratch,
    size_t scratch_capacity,
    size_t *body_capacity,
    size_t max_body)
{
    struct AmiAPIBodyReader reader;
    char *line;
    size_t line_size;
    size_t chunk_size;
    size_t trailer_bytes;
    enum AmiAPIError err;

    memset(&reader, 0, sizeof(reader));

    reader.transport = transport;
    reader.initial = initial;
    reader.initial_size = initial_size;
    reader.scratch = scratch;
    reader.scratch_capacity = scratch_capacity;

    line = (char *)malloc(AMIAPI_CHUNK_LINE_LIMIT + 1);
    if (!line)
        return AMIAPI_ERR_NO_MEMORY;

    for (;;) {
        err = body_reader_line(
            &reader,
            line,
            AMIAPI_CHUNK_LINE_LIMIT + 1,
            &line_size
        );
        if (err != AMIAPI_OK)
            goto done;

        err = parse_chunk_size(line, &chunk_size);
        if (err != AMIAPI_OK)
            goto done;

        if (chunk_size == 0)
            break;

        if (response->body_size > max_body ||
            chunk_size > max_body - response->body_size)
        {
            err = AMIAPI_ERR_RESPONSE_TOO_LARGE;
            goto done;
        }

        err = body_reader_append(
            &reader,
            response,
            body_capacity,
            chunk_size,
            max_body
        );
        if (err != AMIAPI_OK)
            goto done;

        err = body_reader_expect_crlf(&reader);
        if (err != AMIAPI_OK)
            goto done;
    }

    /*
     * Consume optional trailer fields.  We do not expose trailers as
     * regular response headers yet, but they must be syntactically valid
     * and bounded.
     */
    trailer_bytes = 0;

    for (;;) {
        err = body_reader_line(
            &reader,
            line,
            AMIAPI_CHUNK_LINE_LIMIT + 1,
            &line_size
        );
        if (err != AMIAPI_OK)
            goto done;

        if (line_size == 0) {
            err = AMIAPI_OK;
            goto done;
        }

        if (line_size > AMIAPI_HEADER_LIMIT - trailer_bytes) {
            err = AMIAPI_ERR_PROTOCOL;
            goto done;
        }

        trailer_bytes += line_size;

        if (!strchr(line, ':')) {
            err = AMIAPI_ERR_PROTOCOL;
            goto done;
        }
    }

done:
    free(line);
    return err;
}

static struct AmiAPIResponse *amiapi_get_once(
    struct AmiAPIContext *ctx,
    const char *url)
{
    struct ParsedURL parsed;
    struct AmiAPITransport transport;
    struct AmiAPIResponse *response = NULL;
    enum AmiAPIError err;
    char *request = NULL, *header_copy = NULL;
    size_t request_size, raw_used = 0, raw_capacity = 0, header_size, initial_body, body_capacity = 0;
    size_t content_length = 0;
    int has_content_length = 0;
    int is_chunked = 0;
    unsigned char *chunk = NULL, *raw = NULL, *header_end = NULL;
    long n = 0;

    transport.fd = -1;
    transport.tls_ctx = NULL;
    transport.tls_ssl = NULL;
    transport.use_tls = 0;
    if (!ctx || ctx->magic != AMIAPI_CONTEXT_MAGIC || !url) {
        amiapi_set_error(ctx, AMIAPI_ERR_INVALID_ARGUMENT); return NULL;
    }
    amiapi_set_error(ctx, AMIAPI_OK);
    err = parse_http_url(url, &parsed);
    if (err != AMIAPI_OK) { amiapi_set_error(ctx, err); return NULL; }

    request_size = strlen(parsed.path) + strlen(parsed.host) + 128;
    request = (char *)malloc(request_size);
    if (!request) { free_parsed_url(&parsed); amiapi_set_error(ctx, AMIAPI_ERR_NO_MEMORY); return NULL; }
    snprintf(request, request_size,
             "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: AmiAPI/%s\r\nAccept: */*\r\nConnection: close\r\n\r\n",
             parsed.path, parsed.host, AmiAPI_VersionString());

    if (parsed.use_tls)
        err = amiapi_transport_prepare_tls();
    else
        err = AMIAPI_OK;

    if (err == AMIAPI_OK) {
        err = amiapi_transport_connect(
            &transport,
            parsed.host,
            parsed.port
        );
    }

    if (err == AMIAPI_OK && parsed.use_tls) {
        err = amiapi_transport_start_tls(
            &transport,
            parsed.host
        );
    }

    if (err == AMIAPI_OK) {
        err = amiapi_transport_send_all(
            &transport,
            request,
            strlen(request)
        );
    }

    free(request);
    request = NULL;
    free_parsed_url(&parsed);

    if (err != AMIAPI_OK)
        goto fail;

    /* Heap-backed deliberately: classic Amiga process stacks can be tiny. */
    chunk = (unsigned char *)malloc(AMIAPI_READ_CHUNK);
    if (!chunk) { err = AMIAPI_ERR_NO_MEMORY; goto fail; }

    while (!header_end) {
        n = amiapi_transport_receive(&transport, chunk, AMIAPI_READ_CHUNK);
        if (n < 0) { err = AMIAPI_ERR_RECEIVE; goto fail; }
        if (n == 0) { err = AMIAPI_ERR_PROTOCOL; goto fail; }
        err = append_bytes(&raw, &raw_used, &raw_capacity, chunk, (size_t)n,
                           AMIAPI_HEADER_LIMIT + ctx->max_body + 4);
        if (err != AMIAPI_OK) goto fail;
        if (raw_used >= 4) {
            size_t i;
            for (i = 0; i + 3 < raw_used; ++i) {
                if (raw[i]=='\r' && raw[i+1]=='\n' && raw[i+2]=='\r' && raw[i+3]=='\n') {
                    header_end = raw + i; break;
                }
            }
        }
        if (!header_end && raw_used > AMIAPI_HEADER_LIMIT) { err = AMIAPI_ERR_PROTOCOL; goto fail; }
    }

    header_size = (size_t)(header_end - raw) + 4;
    initial_body = raw_used - header_size;
    response = (struct AmiAPIResponse *)calloc(1, sizeof(*response));
    if (!response) { err = AMIAPI_ERR_NO_MEMORY; goto fail; }
    header_copy = (char *)malloc(header_size + 1);
    if (!header_copy) { err = AMIAPI_ERR_NO_MEMORY; goto fail; }
    memcpy(header_copy, raw, header_size); header_copy[header_size] = '\0';
    err = parse_headers(response, header_copy);
    free(header_copy); header_copy = NULL;
    if (err != AMIAPI_OK) goto fail;

    err = transfer_encoding_chunked(
        response,
        &is_chunked
    );
    if (err != AMIAPI_OK)
        goto fail;

    err = parse_content_length(
        response,
        &has_content_length,
        &content_length
    );
    if (err != AMIAPI_OK)
        goto fail;

    /*
     * Transfer-Encoding takes precedence over Content-Length.
     * RFC-compliant senders should not send both, but decoding the
     * transfer coding is authoritative when both are present.
     */
    if (is_chunked) {
        err = decode_chunked_body(
            &transport,
            response,
            raw + header_size,
            initial_body,
            chunk,
            AMIAPI_READ_CHUNK,
            &body_capacity,
            ctx->max_body
        );

        if (err != AMIAPI_OK)
            goto fail;

        free(raw);
        raw = NULL;
        raw_used = raw_capacity = 0;
    }
    else {
        if (has_content_length && content_length > ctx->max_body) {
            err = AMIAPI_ERR_RESPONSE_TOO_LARGE;
            goto fail;
        }

        if (initial_body > ctx->max_body) {
            err = AMIAPI_ERR_RESPONSE_TOO_LARGE;
            goto fail;
        }

        /*
         * Content-Length is authoritative. The first transport read can
         * contain bytes beyond the advertised response body. Since AmiAPI
         * does not reuse this connection, ignore such surplus bytes rather
         * than treating them as part of this response.
         */
        if (has_content_length && initial_body > content_length)
            initial_body = content_length;

        err = append_bytes(
            &response->body,
            &response->body_size,
            &body_capacity,
            raw + header_size,
            initial_body,
            ctx->max_body
        );
        if (err != AMIAPI_OK)
            goto fail;

        free(raw);
        raw = NULL;
        raw_used = raw_capacity = 0;

        if (has_content_length) {
            while (response->body_size < content_length) {
                size_t remaining;
                size_t request_amount;

                remaining = content_length - response->body_size;
                request_amount = remaining < AMIAPI_READ_CHUNK
                    ? remaining
                    : AMIAPI_READ_CHUNK;

                n = amiapi_transport_receive(
                    &transport,
                    chunk,
                    request_amount
                );

                if (n < 0) {
                    err = AMIAPI_ERR_RECEIVE;
                    goto fail;
                }

                if (n == 0) {
                    err = AMIAPI_ERR_PROTOCOL;
                    goto fail;
                }

                err = append_bytes(
                    &response->body,
                    &response->body_size,
                    &body_capacity,
                    chunk,
                    (size_t)n,
                    ctx->max_body
                );
                if (err != AMIAPI_OK)
                    goto fail;
            }
        }
        else {
            while ((n = amiapi_transport_receive(
                        &transport,
                        chunk,
                        AMIAPI_READ_CHUNK)) > 0)
            {
                err = append_bytes(
                    &response->body,
                    &response->body_size,
                    &body_capacity,
                    chunk,
                    (size_t)n,
                    ctx->max_body
                );
                if (err != AMIAPI_OK)
                    goto fail;
            }

            if (n < 0) {
                err = AMIAPI_ERR_RECEIVE;
                goto fail;
            }
        }
    }

    amiapi_transport_close(&transport);
    free(chunk);
    amiapi_set_error(ctx, AMIAPI_OK);
    return response;

fail:
    amiapi_transport_close(&transport);
    free(request); free(header_copy); free(chunk); free(raw);
    AmiAPI_FreeResponse(response);
    amiapi_set_error(ctx, err);
    return NULL;
}


static int is_redirect_status(int status)
{
    return status == 301 ||
           status == 302 ||
           status == 303 ||
           status == 307 ||
           status == 308;
}

static char *build_absolute_url(
    const struct ParsedURL *base,
    const char *path)
{
    const char *scheme;
    int default_port;
    size_t size;
    char *result;

    if (!base || !base->host || !path)
        return NULL;

    scheme = base->use_tls ? "https" : "http";
    default_port = base->use_tls ? 443 : 80;

    size =
        strlen(scheme) +
        3 +
        strlen(base->host) +
        strlen(path) +
        16;

    result = (char *)malloc(size);
    if (!result)
        return NULL;

    if ((int)base->port == default_port) {
        snprintf(
            result,
            size,
            "%s://%s%s",
            scheme,
            base->host,
            path
        );
    }
    else {
        snprintf(
            result,
            size,
            "%s://%s:%u%s",
            scheme,
            base->host,
            (unsigned int)base->port,
            path
        );
    }

    return result;
}

static char *strip_fragment(const char *value)
{
    const char *fragment;
    size_t len;

    if (!value)
        return NULL;

    fragment = strchr(value, '#');
    len = fragment
        ? (size_t)(fragment - value)
        : strlen(value);

    return dup_range(value, len);
}

static char *normalize_url_path(const char *input)
{
    char *work;
    char *query;
    char *result;
    char **segments;
    size_t segment_count;
    size_t max_segments;
    size_t i;
    size_t size;
    char *p;
    char *start;

    if (!input || input[0] != '/')
        return NULL;

    work = dup_range(input, strlen(input));
    if (!work)
        return NULL;

    query = strchr(work, '?');
    if (query) {
        *query = '\0';
        ++query;
    }

    max_segments = strlen(work) / 2 + 2;

    segments = (char **)calloc(max_segments, sizeof(*segments));
    if (!segments) {
        free(work);
        return NULL;
    }

    segment_count = 0;
    p = work + 1;

    while (*p) {
        start = p;

        while (*p && *p != '/')
            ++p;

        if (*p) {
            *p = '\0';
            ++p;
        }

        if (strcmp(start, "") == 0 ||
            strcmp(start, ".") == 0)
        {
            continue;
        }

        if (strcmp(start, "..") == 0) {
            if (segment_count > 0)
                --segment_count;

            continue;
        }

        if (segment_count >= max_segments) {
            free(segments);
            free(work);
            return NULL;
        }

        segments[segment_count++] = start;
    }

    size = 2;

    for (i = 0; i < segment_count; ++i) {
        if (size > (size_t)-1 - strlen(segments[i]) - 1) {
            free(segments);
            free(work);
            return NULL;
        }

        size += strlen(segments[i]) + 1;
    }

    if (query) {
        if (size > (size_t)-1 - strlen(query) - 1) {
            free(segments);
            free(work);
            return NULL;
        }

        size += strlen(query) + 1;
    }

    result = (char *)malloc(size);
    if (!result) {
        free(segments);
        free(work);
        return NULL;
    }

    result[0] = '/';
    result[1] = '\0';

    for (i = 0; i < segment_count; ++i) {
        if (i > 0)
            strcat(result, "/");

        strcat(result, segments[i]);
    }

    if (query) {
        strcat(result, "?");
        strcat(result, query);
    }

    free(segments);
    free(work);

    return result;
}

static enum AmiAPIError resolve_redirect_url(
    const char *base_url,
    const char *location,
    char **resolved)
{
    struct ParsedURL base;
    char *clean_location = NULL;
    char *base_path = NULL;
    char *combined = NULL;
    char *normalized = NULL;
    char *slash;
    char *query;
    size_t directory_len;
    size_t location_len;
    enum AmiAPIError err;

    if (!base_url || !location || !resolved)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    *resolved = NULL;

    clean_location = strip_fragment(location);
    if (!clean_location)
        return AMIAPI_ERR_NO_MEMORY;

    /*
     * A fragment-only redirect resolves to the same resource.
     * Since fragments are not transmitted to the server, this would just
     * create a redirect loop. Treat it as a protocol error.
     */
    if (*clean_location == '\0') {
        free(clean_location);
        return AMIAPI_ERR_PROTOCOL;
    }

    /*
     * Absolute redirect.
     */
    if (strncmp(clean_location, "http://", 7) == 0 ||
        strncmp(clean_location, "https://", 8) == 0)
    {
        *resolved = clean_location;
        return AMIAPI_OK;
    }

    if (strstr(clean_location, "://")) {
        free(clean_location);
        return AMIAPI_ERR_UNSUPPORTED_SCHEME;
    }

    err = parse_http_url(base_url, &base);
    if (err != AMIAPI_OK) {
        free(clean_location);
        return err;
    }

    /*
     * Query-only redirect:
     *
     *     Location: ?page=2
     *
     * Keep the current path and replace its query.
     */
    if (clean_location[0] == '?') {
        query = strchr(base.path, '?');

        directory_len = query
            ? (size_t)(query - base.path)
            : strlen(base.path);

        location_len = strlen(clean_location);

        combined = (char *)malloc(
            directory_len + location_len + 1
        );

        if (!combined) {
            free(clean_location);
            free_parsed_url(&base);
            return AMIAPI_ERR_NO_MEMORY;
        }

        memcpy(combined, base.path, directory_len);
        memcpy(
            combined + directory_len,
            clean_location,
            location_len + 1
        );

        *resolved = build_absolute_url(&base, combined);

        free(combined);
        free(clean_location);
        free_parsed_url(&base);

        return *resolved
            ? AMIAPI_OK
            : AMIAPI_ERR_NO_MEMORY;
    }

    /*
     * Root-relative redirect.
     */
    if (clean_location[0] == '/') {
        normalized = normalize_url_path(clean_location);

        if (!normalized) {
            free(clean_location);
            free_parsed_url(&base);
            return AMIAPI_ERR_NO_MEMORY;
        }

        *resolved = build_absolute_url(&base, normalized);

        free(normalized);
        free(clean_location);
        free_parsed_url(&base);

        return *resolved
            ? AMIAPI_OK
            : AMIAPI_ERR_NO_MEMORY;
    }

    /*
     * Relative redirect. Strip the existing query from the base path before
     * resolving against the base directory.
     */
    base_path = dup_range(base.path, strlen(base.path));
    if (!base_path) {
        free(clean_location);
        free_parsed_url(&base);
        return AMIAPI_ERR_NO_MEMORY;
    }

    query = strchr(base_path, '?');
    if (query)
        *query = '\0';

    slash = strrchr(base_path, '/');

    if (slash)
        directory_len = (size_t)(slash - base_path) + 1;
    else
        directory_len = 1;

    location_len = strlen(clean_location);

    if (directory_len > (size_t)-1 - location_len - 1) {
        free(base_path);
        free(clean_location);
        free_parsed_url(&base);
        return AMIAPI_ERR_NO_MEMORY;
    }

    combined = (char *)malloc(
        directory_len + location_len + 1
    );

    if (!combined) {
        free(base_path);
        free(clean_location);
        free_parsed_url(&base);
        return AMIAPI_ERR_NO_MEMORY;
    }

    memcpy(combined, base_path, directory_len);
    memcpy(
        combined + directory_len,
        clean_location,
        location_len + 1
    );

    normalized = normalize_url_path(combined);

    free(combined);
    free(base_path);
    free(clean_location);

    if (!normalized) {
        free_parsed_url(&base);
        return AMIAPI_ERR_NO_MEMORY;
    }

    *resolved = build_absolute_url(&base, normalized);

    free(normalized);
    free_parsed_url(&base);

    return *resolved
        ? AMIAPI_OK
        : AMIAPI_ERR_NO_MEMORY;
}

struct AmiAPIResponse *AmiAPI_Get(
    struct AmiAPIContext *ctx,
    const char *url)
{
    struct AmiAPIResponse *response;
    char *current_url;
    char *next_url;
    const char *location;
    enum AmiAPIError err;
    int redirects;

    if (!ctx ||
        ctx->magic != AMIAPI_CONTEXT_MAGIC ||
        !url)
    {
        amiapi_set_error(ctx, AMIAPI_ERR_INVALID_ARGUMENT);
        return NULL;
    }

    current_url = dup_range(url, strlen(url));
    if (!current_url) {
        amiapi_set_error(ctx, AMIAPI_ERR_NO_MEMORY);
        return NULL;
    }

    for (redirects = 0; ; ++redirects) {
        response = amiapi_get_once(ctx, current_url);

        if (!response) {
            free(current_url);
            return NULL;
        }

        if (!is_redirect_status(response->status)) {
            free(current_url);
            amiapi_set_error(ctx, AMIAPI_OK);
            return response;
        }

        if (redirects >= AMIAPI_MAX_REDIRECTS) {
            AmiAPI_FreeResponse(response);
            free(current_url);
            amiapi_set_error(ctx, AMIAPI_ERR_PROTOCOL);
            return NULL;
        }

        location = AmiAPI_Header(response, "Location");
        if (!location || !*location) {
            AmiAPI_FreeResponse(response);
            free(current_url);
            amiapi_set_error(ctx, AMIAPI_ERR_PROTOCOL);
            return NULL;
        }

        next_url = NULL;

        err = resolve_redirect_url(
            current_url,
            location,
            &next_url
        );

        AmiAPI_FreeResponse(response);

        if (err != AMIAPI_OK) {
            free(current_url);
            amiapi_set_error(ctx, err);
            return NULL;
        }

        free(current_url);
        current_url = next_url;
    }
}

void AmiAPI_FreeResponse(struct AmiAPIResponse *response)
{
    size_t i;
    if (!response) return;
    for (i = 0; i < response->header_count; ++i) {
        free(response->headers[i].name); free(response->headers[i].value);
    }
    free(response->headers); free(response->body); free(response);
}
int AmiAPI_Status(const struct AmiAPIResponse *r) { return r ? r->status : 0; }
const unsigned char *AmiAPI_Body(const struct AmiAPIResponse *r) { return r ? r->body : NULL; }
size_t AmiAPI_BodySize(const struct AmiAPIResponse *r) { return r ? r->body_size : 0; }
const char *AmiAPI_Header(const struct AmiAPIResponse *r, const char *name)
{
    size_t i;
    if (!r || !name) return NULL;
    for (i = 0; i < r->header_count; ++i)
        if (ascii_equal_ci(r->headers[i].name, name)) return r->headers[i].value;
    return NULL;
}
