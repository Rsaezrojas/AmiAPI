#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <amiapi/amiapi.h>

static int hex_value(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static unsigned char *decode_hex(
    const char *text,
    size_t *size_out)
{
    size_t len;
    size_t i;
    unsigned char *data;

    if (!text || !size_out)
        return NULL;

    len = strlen(text);

    if ((len & 1) != 0)
        return NULL;

    data = (unsigned char *)malloc(len / 2 + 1);
    if (!data)
        return NULL;

    for (i = 0; i < len; i += 2) {
        int hi = hex_value((unsigned char)text[i]);
        int lo = hex_value((unsigned char)text[i + 1]);

        if (hi < 0 || lo < 0) {
            free(data);
            return NULL;
        }

        data[i / 2] = (unsigned char)((hi << 4) | lo);
    }

    *size_out = len / 2;
    return data;
}

int main(int argc, char **argv)
{
    struct AmiAPIContext *ctx;
    struct AmiAPIRequest *request;
    struct AmiAPIResponse *response;
    unsigned char *body = NULL;
    size_t body_size = 0;
    enum AmiAPIError err;

    if (argc != 4) {
        fprintf(
            stderr,
            "usage: %s METHOD URL HEXBODY\n",
            argv[0]
        );
        return 2;
    }

    body = decode_hex(argv[3], &body_size);

    if (!body && argv[3][0] != '\0') {
        fprintf(stderr, "invalid hex body\n");
        return 3;
    }

    ctx = AmiAPI_Create();
    if (!ctx) {
        free(body);
        return 4;
    }

    request = AmiAPI_RequestCreate(
        ctx,
        argv[1],
        argv[2]
    );

    if (!request) {
        fprintf(
            stderr,
            "request-create failed: %s\n",
            AmiAPI_ErrorString(AmiAPI_LastError(ctx))
        );
        free(body);
        AmiAPI_Destroy(ctx);
        return 5;
    }

    if (body_size != 0) {
        err = AmiAPI_RequestSetBody(
            request,
            body,
            body_size
        );

        if (err != AMIAPI_OK) {
            fprintf(
                stderr,
                "set-body failed: %s\n",
                AmiAPI_ErrorString(err)
            );

            free(body);
            AmiAPI_RequestDestroy(request);
            AmiAPI_Destroy(ctx);
            return 6;
        }
    }

    /*
     * Prove that AmiAPI_RequestSetBody() owns its own copy.
     */
    free(body);
    body = NULL;

    response = AmiAPI_RequestExecute(request);

    if (!response) {
        fprintf(
            stderr,
            "request failed: %s\n",
            AmiAPI_ErrorString(AmiAPI_LastError(ctx))
        );

        AmiAPI_RequestDestroy(request);
        AmiAPI_Destroy(ctx);
        return 20;
    }

    printf("HTTP %d\n", AmiAPI_Status(response));
    printf(
        "body-bytes: %lu\n",
        (unsigned long)AmiAPI_BodySize(response)
    );

    AmiAPI_FreeResponse(response);
    AmiAPI_RequestDestroy(request);
    AmiAPI_Destroy(ctx);

    return 0;
}
