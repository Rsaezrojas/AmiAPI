#include <stdio.h>
#include <string.h>

#include <amiapi/form.h>

static int expect_form(
    const char *name1,
    const char *value1,
    const char *name2,
    const char *value2,
    const char *expected)
{
    struct AmiAPIForm *form;
    enum AmiAPIError err;
    const char *body;

    form = AmiAPI_FormCreate();

    if (!form) {
        fprintf(stderr, "FormCreate failed\n");
        return 0;
    }

    err = AmiAPI_FormAdd(
        form,
        name1,
        value1
    );

    if (err != AMIAPI_OK) {
        fprintf(stderr, "first FormAdd failed: %d\n", (int)err);
        AmiAPI_FormDestroy(form);
        return 0;
    }

    if (name2) {
        err = AmiAPI_FormAdd(
            form,
            name2,
            value2
        );

        if (err != AMIAPI_OK) {
            fprintf(stderr, "second FormAdd failed: %d\n", (int)err);
            AmiAPI_FormDestroy(form);
            return 0;
        }
    }

    body = AmiAPI_FormBody(form);

    if (!body || strcmp(body, expected) != 0) {
        fprintf(
            stderr,
            "form mismatch:\n"
            "  got:      %s\n"
            "  expected: %s\n",
            body ? body : "(null)",
            expected
        );

        AmiAPI_FormDestroy(form);
        return 0;
    }

    if (AmiAPI_FormSize(form) != strlen(expected)) {
        fprintf(
            stderr,
            "form-size mismatch: got=%lu expected=%lu\n",
            (unsigned long)AmiAPI_FormSize(form),
            (unsigned long)strlen(expected)
        );

        AmiAPI_FormDestroy(form);
        return 0;
    }

    AmiAPI_FormDestroy(form);
    return 1;
}

int main(void)
{
    struct AmiAPIForm *empty;

    empty = AmiAPI_FormCreate();

    if (!empty ||
        !AmiAPI_FormBody(empty) ||
        strcmp(AmiAPI_FormBody(empty), "") != 0 ||
        AmiAPI_FormSize(empty) != 0)
    {
        fprintf(stderr, "empty form failed\n");
        AmiAPI_FormDestroy(empty);
        return 1;
    }

    AmiAPI_FormDestroy(empty);

    if (!expect_form(
            "q",
            "Amiga 1200",
            NULL,
            NULL,
            "q=Amiga+1200"))
        return 1;

    if (!expect_form(
            "symbol",
            "+",
            NULL,
            NULL,
            "symbol=%2B"))
        return 1;

    if (!expect_form(
            "value",
            "a&b=c",
            NULL,
            NULL,
            "value=a%26b%3Dc"))
        return 1;

    if (!expect_form(
            "empty",
            "",
            NULL,
            NULL,
            "empty="))
        return 1;

    if (!expect_form(
            "grant_type",
            "authorization_code",
            "redirect uri",
            "https://example.test/a b",
            "grant_type=authorization_code&"
            "redirect+uri=https%3A%2F%2Fexample.test%2Fa+b"))
        return 1;

    /*
     * UTF-8 bytes are percent-encoded byte-for-byte.
     * "ø" = C3 B8.
     */
    if (!expect_form(
            "name",
            "\xc3\xb8",
            NULL,
            NULL,
            "name=%C3%B8"))
        return 1;

    puts("Form encoding tests: OK");
    return 0;
}
