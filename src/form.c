#include <stdlib.h>
#include <string.h>

#include <amiapi/form.h>

struct AmiAPIForm {
    char *body;
    size_t length;
    size_t capacity;
};

static int is_form_unreserved(unsigned char c)
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

static size_t encoded_length(const char *text)
{
    const unsigned char *p;
    size_t length;

    p = (const unsigned char *)text;
    length = 0;

    while (*p) {
        if (*p == ' ' || is_form_unreserved(*p)) {
            if (length == (size_t)-1)
                return (size_t)-1;

            length++;
        }
        else {
            if (length > (size_t)-1 - 3)
                return (size_t)-1;

            length += 3;
        }

        ++p;
    }

    return length;
}

static void encode_into(
    char *output,
    size_t *position,
    const char *text)
{
    const unsigned char *p;
    size_t out;

    p = (const unsigned char *)text;
    out = *position;

    while (*p) {
        unsigned char c;

        c = *p++;

        if (c == ' ') {
            output[out++] = '+';
        }
        else if (is_form_unreserved(c)) {
            output[out++] = (char)c;
        }
        else {
            output[out++] = '%';
            output[out++] = hex_digit(c >> 4);
            output[out++] = hex_digit(c);
        }
    }

    *position = out;
}

static enum AmiAPIError ensure_capacity(
    struct AmiAPIForm *form,
    size_t needed)
{
    char *new_body;
    size_t capacity;

    if (needed <= form->capacity)
        return AMIAPI_OK;

    capacity = form->capacity
        ? form->capacity
        : 64;

    while (capacity < needed) {
        if (capacity > ((size_t)-1) / 2) {
            capacity = needed;
            break;
        }

        capacity *= 2;
    }

    new_body = (char *)realloc(
        form->body,
        capacity
    );

    if (!new_body)
        return AMIAPI_ERR_NO_MEMORY;

    form->body = new_body;
    form->capacity = capacity;

    return AMIAPI_OK;
}

struct AmiAPIForm *AmiAPI_FormCreate(void)
{
    struct AmiAPIForm *form;

    form = (struct AmiAPIForm *)calloc(
        1,
        sizeof(*form)
    );

    if (!form)
        return NULL;

    form->body = (char *)malloc(1);

    if (!form->body) {
        free(form);
        return NULL;
    }

    form->body[0] = '\0';
    form->capacity = 1;

    return form;
}

enum AmiAPIError AmiAPI_FormAdd(
    struct AmiAPIForm *form,
    const char *name,
    const char *value)
{
    size_t name_length;
    size_t value_length;
    size_t extra;
    size_t needed;
    size_t position;
    enum AmiAPIError err;

    if (!form || !name || !value)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    name_length = encoded_length(name);
    value_length = encoded_length(value);

    if (name_length == (size_t)-1 ||
        value_length == (size_t)-1)
    {
        return AMIAPI_ERR_NO_MEMORY;
    }

    extra =
        (form->length ? 1 : 0) +
        name_length +
        1 +
        value_length;

    if (form->length > (size_t)-1 - extra - 1)
        return AMIAPI_ERR_NO_MEMORY;

    needed = form->length + extra + 1;

    err = ensure_capacity(form, needed);

    if (err != AMIAPI_OK)
        return err;

    position = form->length;

    if (position != 0)
        form->body[position++] = '&';

    encode_into(
        form->body,
        &position,
        name
    );

    form->body[position++] = '=';

    encode_into(
        form->body,
        &position,
        value
    );

    form->body[position] = '\0';
    form->length = position;

    return AMIAPI_OK;
}

const char *AmiAPI_FormBody(
    const struct AmiAPIForm *form)
{
    return form ? form->body : NULL;
}

size_t AmiAPI_FormSize(
    const struct AmiAPIForm *form)
{
    return form ? form->length : 0;
}

enum AmiAPIError AmiAPI_RequestSetForm(
    struct AmiAPIRequest *request,
    const struct AmiAPIForm *form)
{
    enum AmiAPIError err;

    if (!request || !form)
        return AMIAPI_ERR_INVALID_ARGUMENT;

    err = AmiAPI_RequestSetBody(
        request,
        form->body,
        form->length
    );

    if (err != AMIAPI_OK)
        return err;

    return AmiAPI_RequestSetHeader(
        request,
        "Content-Type",
        "application/x-www-form-urlencoded"
    );
}

void AmiAPI_FormDestroy(
    struct AmiAPIForm *form)
{
    if (!form)
        return;

    free(form->body);
    free(form);
}
