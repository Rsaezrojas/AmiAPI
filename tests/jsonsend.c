#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <amiapi/amiapi.h>
#include <amiapi/json_http.h>

int main(int argc, char **argv)
{
    struct AmiAPIContext *ctx;
    struct AmiAPIRequest *request;
    struct AmiAPIResponse *response;
    enum AmiAPIError err;
    const char *json;

    if (argc != 3) {
        fprintf(
            stderr,
            "usage: %s URL JSON\n",
            argv[0]
        );
        return 2;
    }

    json = argv[2];

    ctx = AmiAPI_Create();
    if (!ctx)
        return 3;

    request = AmiAPI_RequestCreate(
        ctx,
        "POST",
        argv[1]
    );

    if (!request) {
        AmiAPI_Destroy(ctx);
        return 4;
    }

    err = AmiAPI_RequestSetJSON(
        request,
        json,
        strlen(json)
    );

    if (err != AMIAPI_OK) {
        fprintf(
            stderr,
            "set-json failed: %s\n",
            AmiAPI_ErrorString(err)
        );

        AmiAPI_RequestDestroy(request);
        AmiAPI_Destroy(ctx);
        return 5;
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

    AmiAPI_FreeResponse(response);
    AmiAPI_RequestDestroy(request);
    AmiAPI_Destroy(ctx);

    return 0;
}
