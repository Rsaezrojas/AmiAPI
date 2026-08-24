#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <amiapi/amiapi.h>
#include "internal.h"

#define AMIAPI_HEADER_LIMIT 32768UL
#define AMIAPI_READ_CHUNK 4096UL

struct AmiAPIHeader {
    char *name;
    char *value;
};

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
};

static char *dup_range(const char *start, size_t len)
{
    char *s = (char *)malloc(len + 1);
    if (s == NULL)
        return NULL;
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
        if (tolower(ca) != tolower(cb))
            return 0;
    }
    return *a == '\0' && *b == '\0';
}

static void free_parsed_url(struct ParsedURL *url)
{
    if (url == NULL)
        return;
    free(url->host);
    free(url->path);
    url->host = NULL;
    url->path = NULL;
}

static enum AmiAPIError parse_http_url(const char *input, struct ParsedURL *out)
{
    const char *p;
    const char *host_start;
    const char *host_end;
    const char *path_start;
    const char *colon;
    unsigned long port;
    size_t host_len;

    memset(out, 0, sizeof(*out));

    if (strncmp(input, "http://", 7) != 0) {
        if (strstr(input, "://") != NULL)
            return AMIAPI_ERR_UNSUPPORTED_SCHEME;
        return AMIAPI_ERR_BAD_URL;
    }

    host_start = input + 7;
    if (*host_start == '\0')
        return AMIAPI_ERR_BAD_URL;

    path_start = strchr(host_start, '/');
    host_end = path_start != NULL ? path_start : input + strlen(input);

    colon = NULL;
    for (p = host_start; p < host_end; ++p) {
        if (*p == ':')
            colon = p;
    }

    port = 80;
    if (colon != NULL) {
        char *endptr;
        if (colon == host_start || colon + 1 == host_end)
            return AMIAPI_ERR_BAD_URL;
        port = strtoul(colon + 1, &endptr, 10);
        if (endptr != host_end || port == 0 || port > 65535)
            return AMIAPI_ERR_BAD_URL;
        host_end = colon;
    }

    host_len = (size_t)(host_end - host_start);
    if (host_len == 0)
        return AMIAPI_ERR_BAD_URL;

    out->host = dup_range(host_start, host_len);
    if (out->host == NULL)
        return AMIAPI_ERR_NO_MEMORY;

    out->path = path_start != NULL ? dup_range(path_start, strlen(path_start))
                                   : dup_range("/", 1);
    if (out->path == NULL) {
        free_parsed_url(out);
        return AMIAPI_ERR_NO_MEMORY;
    }

    out->port = (unsigned short)port;
    return AMIAPI_OK;
}

static char *trim_left(char *s)
{
    while (*s == ' ' || *s == '\t')
        ++s;
    return s;
}

static void trim_right(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
}

static enum AmiAPIError add_header(struct AmiAPIResponse *response,
                                   const char *name, const char *value)
{
    struct AmiAPIHeader *new_headers;
    char *n;
    char *v;

    n = dup_range(name, strlen(name));
    v = dup_range(value, strlen(value));
    if (n == NULL || v == NULL) {
        free(n);
        free(v);
        return AMIAPI_ERR_NO_MEMORY;
    }

    new_headers = (struct AmiAPIHeader *)realloc(
        response->headers,
        (response->header_count + 1) * sizeof(*response->headers));
    if (new_headers == NULL) {
        free(n);
        free(v);
        return AMIAPI_ERR_NO_MEMORY;
    }

    response->headers = new_headers;
    response->headers[response->header_count].name = n;
    response->headers[response->header_count].value = v;
    response->header_count++;
    return AMIAPI_OK;
}

static enum AmiAPIError parse_headers(struct AmiAPIResponse *response,
                                      char *header_text)
{
    char *line;
    char *next;
    char *colon;
    int status;

    line = header_text;
    next = strstr(line, "\r\n");
    if (next == NULL)
        return AMIAPI_ERR_PROTOCOL;
    *next = '\0';

    if (sscanf(line, "HTTP/%*u.%*u %d", &status) != 1 ||
        status < 100 || status > 999)
        return AMIAPI_ERR_PROTOCOL;
    response->status = status;

    line = next + 2;
    while (*line != '\0') {
        next = strstr(line, "\r\n");
        if (next == NULL)
            return AMIAPI_ERR_PROTOCOL;
        *next = '\0';

        if (*line == '\0')
            break;

        colon = strchr(line, ':');
        if (colon == NULL)
            return AMIAPI_ERR_PROTOCOL;
        *colon = '\0';
        trim_right(line);
        colon = trim_left(colon + 1);
        trim_right(colon);

        if (*line == '\0')
            return AMIAPI_ERR_PROTOCOL;

        {
            enum AmiAPIError err = add_header(response, line, colon);
            if (err != AMIAPI_OK)
                return err;
        }
        line = next + 2;
    }

    return AMIAPI_OK;
}

static enum AmiAPIError append_bytes(unsigned char **buffer, size_t *used,
                                     size_t *capacity, const void *data,
                                     size_t amount, size_t limit)
{
    size_t needed;
    size_t new_capacity;
    unsigned char *new_buffer;

    if (amount > limit - *used)
        return AMIAPI_ERR_RESPONSE_TOO_LARGE;

    needed = *used + amount;
    if (needed > *capacity) {
        new_capacity = *capacity == 0 ? AMIAPI_READ_CHUNK : *capacity;
        while (new_capacity < needed) {
            size_t doubled = new_capacity * 2;
            if (doubled < new_capacity || doubled > limit)
                new_capacity = limit;
            else
                new_capacity = doubled;
            if (new_capacity < needed && new_capacity == limit)
                return AMIAPI_ERR_RESPONSE_TOO_LARGE;
        }

        new_buffer = (unsigned char *)realloc(*buffer, new_capacity);
        if (new_buffer == NULL)
            return AMIAPI_ERR_NO_MEMORY;
        *buffer = new_buffer;
        *capacity = new_capacity;
    }

    memcpy(*buffer + *used, data, amount);
    *used = needed;
    return AMIAPI_OK;
}

struct AmiAPIResponse *AmiAPI_Get(struct AmiAPIContext *ctx, const char *url)
{
    struct ParsedURL parsed;
    struct AmiAPITransport transport;
    struct AmiAPIResponse *response;
    enum AmiAPIError err;
    char *request;
    size_t request_size;
    unsigned char chunk[AMIAPI_READ_CHUNK];
    unsigned char *raw;
    size_t raw_used, raw_capacity;
    long n;
    unsigned char *header_end;
    size_t header_size, initial_body;
    char *header_copy;

    if (ctx == NULL || ctx->magic != AMIAPI_CONTEXT_MAGIC || url == NULL) {
        amiapi_set_error(ctx, AMIAPI_ERR_INVALID_ARGUMENT);
        return NULL;
    }

    amiapi_set_error(ctx, AMIAPI_OK);
    err = parse_http_url(url, &parsed);
    if (err != AMIAPI_OK) {
        amiapi_set_error(ctx, err);
        return NULL;
    }

    request_size = strlen(parsed.path) + strlen(parsed.host) + 128;
    request = (char *)malloc(request_size);
    if (request == NULL) {
        free_parsed_url(&parsed);
        amiapi_set_error(ctx, AMIAPI_ERR_NO_MEMORY);
        return NULL;
    }

    snprintf(request, request_size,
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: AmiAPI/%s\r\n"
             "Accept: */*\r\n"
             "Connection: close\r\n\r\n",
             parsed.path, parsed.host, AmiAPI_VersionString());

    transport.fd = -1;
    err = amiapi_transport_connect(&transport, parsed.host, parsed.port);
    if (err == AMIAPI_OK)
        err = amiapi_transport_send_all(&transport, request, strlen(request));
    free(request);
    free_parsed_url(&parsed);

    if (err != AMIAPI_OK) {
        amiapi_transport_close(&transport);
        amiapi_set_error(ctx, err);
        return NULL;
    }

    raw = NULL;
    raw_used = raw_capacity = 0;
    header_end = NULL;

    while (header_end == NULL) {
        n = amiapi_transport_receive(&transport, chunk, sizeof(chunk));
        if (n < 0) {
            err = AMIAPI_ERR_RECEIVE;
            goto fail_raw;
        }
        if (n == 0) {
            err = AMIAPI_ERR_PROTOCOL;
            goto fail_raw;
        }

        err = append_bytes(&raw, &raw_used, &raw_capacity, chunk, (size_t)n,
                           AMIAPI_HEADER_LIMIT + ctx->max_body + 4);
        if (err != AMIAPI_OK)
            goto fail_raw;

        if (raw_used >= 4) {
            size_t i;
            for (i = 0; i + 3 < raw_used; ++i) {
                if (raw[i] == '\r' && raw[i + 1] == '\n' &&
                    raw[i + 2] == '\r' && raw[i + 3] == '\n') {
                    header_end = raw + i;
                    break;
                }
            }
        }
        if (header_end == NULL && raw_used > AMIAPI_HEADER_LIMIT) {
            err = AMIAPI_ERR_PROTOCOL;
            goto fail_raw;
        }
    }

    header_size = (size_t)(header_end - raw) + 4;
    initial_body = raw_used - header_size;

    response = (struct AmiAPIResponse *)calloc(1, sizeof(*response));
    if (response == NULL) {
        err = AMIAPI_ERR_NO_MEMORY;
        goto fail_raw;
    }

    header_copy = (char *)malloc(header_size + 1);
    if (header_copy == NULL) {
        err = AMIAPI_ERR_NO_MEMORY;
        goto fail_response;
    }
    memcpy(header_copy, raw, header_size);
    header_copy[header_size] = '\0';

    err = parse_headers(response, header_copy);
    free(header_copy);
    if (err != AMIAPI_OK)
        goto fail_response;

    response->body = NULL;
    response->body_size = 0;
    raw_capacity = 0;

    if (initial_body > ctx->max_body) {
        err = AMIAPI_ERR_RESPONSE_TOO_LARGE;
        goto fail_response;
    }

    if (initial_body != 0) {
        response->body = (unsigned char *)malloc(initial_body + 1);
        if (response->body == NULL) {
            err = AMIAPI_ERR_NO_MEMORY;
            goto fail_response;
        }
        memcpy(response->body, raw + header_size, initial_body);
        response->body_size = initial_body;
        raw_capacity = initial_body + 1;
    }
    free(raw);
    raw = NULL;

    while ((n = amiapi_transport_receive(&transport, chunk, sizeof(chunk))) > 0) {
        err = append_bytes(&response->body, &response->body_size, &raw_capacity,
                           chunk, (size_t)n, ctx->max_body);
        if (err != AMIAPI_OK)
            goto fail_response;
    }

    amiapi_transport_close(&transport);
    if (n < 0) {
        err = AMIAPI_ERR_RECEIVE;
        goto fail_response;
    }

    if (response->body == NULL) {
        response->body = (unsigned char *)malloc(1);
        if (response->body == NULL) {
            err = AMIAPI_ERR_NO_MEMORY;
            goto fail_response;
        }
    } else if (raw_capacity <= response->body_size) {
        unsigned char *p = (unsigned char *)realloc(
            response->body, response->body_size + 1);
        if (p == NULL) {
            err = AMIAPI_ERR_NO_MEMORY;
            goto fail_response;
        }
        response->body = p;
    }
    response->body[response->body_size] = '\0';

    amiapi_set_error(ctx, AMIAPI_OK);
    return response;

fail_response:
    amiapi_transport_close(&transport);
    free(raw);
    AmiAPI_FreeResponse(response);
    amiapi_set_error(ctx, err);
    return NULL;

fail_raw:
    amiapi_transport_close(&transport);
    free(raw);
    amiapi_set_error(ctx, err);
    return NULL;
}

void AmiAPI_FreeResponse(struct AmiAPIResponse *response)
{
    size_t i;

    if (response == NULL)
        return;

    for (i = 0; i < response->header_count; ++i) {
        free(response->headers[i].name);
        free(response->headers[i].value);
    }
    free(response->headers);
    free(response->body);
    free(response);
}

int AmiAPI_Status(const struct AmiAPIResponse *response)
{
    return response != NULL ? response->status : 0;
}

const unsigned char *AmiAPI_Body(const struct AmiAPIResponse *response)
{
    return response != NULL ? response->body : NULL;
}

size_t AmiAPI_BodySize(const struct AmiAPIResponse *response)
{
    return response != NULL ? response->body_size : 0;
}

const char *AmiAPI_Header(const struct AmiAPIResponse *response,
                          const char *name)
{
    size_t i;

    if (response == NULL || name == NULL)
        return NULL;

    for (i = 0; i < response->header_count; ++i) {
        if (ascii_equal_ci(response->headers[i].name, name))
            return response->headers[i].value;
    }
    return NULL;
}
