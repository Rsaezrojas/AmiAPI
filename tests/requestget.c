#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <amiapi/amiapi.h>

static int set_header(
    struct AmiAPIRequest *request,
    const char *name,
    const char *value)
{
    enum AmiAPIError err;

    err = AmiAPI_RequestSetHeader(request, name, value);

    if (err != AMIAPI_OK) {
        fprintf(
            stderr,
            "set-header failed: %s\n",
            AmiAPI_ErrorString(err)
        );
        return 0;
    }

    return 1;
}

int main(int argc, char **argv)
{
    struct AmiAPIContext *ctx;
    struct AmiAPIRequest *request;
    struct AmiAPIResponse *response;
    int i;

    if (argc < 2) {
        fprintf(
            stderr,
            "usage: %s URL [NAME VALUE ...]\n",
            argv[0]
        );
        return 2;
    }

    if ((argc - 2) % 2 != 0) {
        fprintf(stderr, "headers must be NAME VALUE pairs\n");
        return 2;
    }

    ctx = AmiAPI_Create();
    if (!ctx) {
        fprintf(stderr, "AmiAPI_Create failed\n");
        return 3;
    }

    request = AmiAPI_RequestCreate(
        ctx,
        "GET",
        argv[1]
    );

    if (!request) {
        fprintf(
            stderr,
            "request-create failed: %s\n",
            AmiAPI_ErrorString(AmiAPI_LastError(ctx))
        );
        AmiAPI_Destroy(ctx);
        return 4;
    }

    for (i = 2; i < argc; i += 2) {
        if (!set_header(
                request,
                argv[i],
                argv[i + 1]))
        {
            AmiAPI_RequestDestroy(request);
            AmiAPI_Destroy(ctx);
            return 5;
        }
    }

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
    printf("body-bytes: %lu\n",
           (unsigned long)AmiAPI_BodySize(response));

    AmiAPI_FreeResponse(response);
    AmiAPI_RequestDestroy(request);
    AmiAPI_Destroy(ctx);

    return 0;
}
