#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <amiapi/amiapi.h>
#include "internal.h"

#define AMIAPI_HEADER_LIMIT 32768UL
#define AMIAPI_READ_CHUNK 4096UL

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
    const char *p;
    const char *host_start;
    const char *host_end;
    const char *path_start;
    const char *colon;
    unsigned long port;
    size_t host_len;
    size_t scheme_len;

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

    host_end = path_start
        ? path_start
        : input + strlen(input);

    colon = NULL;

    for (p = host_start; p < host_end; ++p) {
        if (*p == ':')
            colon = p;
    }

    if (colon) {
        char *endptr;

        if (colon == host_start || colon + 1 == host_end)
            return AMIAPI_ERR_BAD_URL;

        port = strtoul(colon + 1, &endptr, 10);

        if (endptr != host_end ||
            port == 0 ||
            port > 65535)
            return AMIAPI_ERR_BAD_URL;

        host_end = colon;
    }

    host_len = (size_t)(host_end - host_start);

    if (!host_len)
        return AMIAPI_ERR_BAD_URL;

    out->host = dup_range(host_start, host_len);

    if (!out->host)
        return AMIAPI_ERR_NO_MEMORY;

    out->path = path_start
        ? dup_range(path_start, strlen(path_start))
        : dup_range("/", 1);

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

struct AmiAPIResponse *AmiAPI_Get(struct AmiAPIContext *ctx, const char *url)
{
    struct ParsedURL parsed;
    struct AmiAPITransport transport;
    struct AmiAPIResponse *response = NULL;
    enum AmiAPIError err;
    char *request = NULL, *header_copy = NULL;
    size_t request_size, raw_used = 0, raw_capacity = 0, header_size, initial_body, body_capacity = 0;
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

    if (initial_body > ctx->max_body) { err = AMIAPI_ERR_RESPONSE_TOO_LARGE; goto fail; }
    err = append_bytes(&response->body, &response->body_size, &body_capacity,
                       raw + header_size, initial_body, ctx->max_body);
    if (err != AMIAPI_OK) goto fail;
    free(raw); raw = NULL; raw_used = raw_capacity = 0;

    while ((n = amiapi_transport_receive(&transport, chunk, AMIAPI_READ_CHUNK)) > 0) {
        err = append_bytes(&response->body, &response->body_size, &body_capacity,
                           chunk, (size_t)n, ctx->max_body);
        if (err != AMIAPI_OK) goto fail;
    }
    if (n < 0) { err = AMIAPI_ERR_RECEIVE; goto fail; }

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
