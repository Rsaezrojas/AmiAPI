#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <amiapi/amiapi.h>

int main(int argc, char **argv)
{
    struct AmiAPIContext *ctx;
    struct AmiAPIRequest *request;
    enum AmiAPIError err;
    int i;

    if (argc < 6 || (argc - 6) % 2 != 0) {
        fprintf(stderr,
            "usage: %s METHOD URL PATH MAX_BODY TIMEOUT [NAME VALUE ...]\n",
            argv[0]);
        return 2;
    }

    ctx = AmiAPI_Create();
    if (!ctx)
        return 3;

    AmiAPI_SetMaxBody(ctx, (size_t)strtoul(argv[4], NULL, 10));
    AmiAPI_SetTimeout(ctx, (unsigned int)strtoul(argv[5], NULL, 10));

    if (strcmp(argv[1], "CONVENIENCE") == 0) {
        err = AmiAPI_DownloadFile(ctx, argv[2], argv[3]);
    }
    else {
        request = AmiAPI_RequestCreate(ctx, argv[1], argv[2]);
        if (!request) {
            AmiAPI_Destroy(ctx);
            return 4;
        }

        for (i = 6; i < argc; i += 2) {
            err = AmiAPI_RequestSetHeader(request, argv[i], argv[i + 1]);
            if (err != AMIAPI_OK) {
                AmiAPI_RequestDestroy(request);
                AmiAPI_Destroy(ctx);
                return 5;
            }
        }

        err = AmiAPI_RequestDownloadFile(request, argv[3]);
        AmiAPI_RequestDestroy(request);
    }

    printf("error: %d\n", (int)err);
    printf("last-error: %d\n", (int)AmiAPI_LastError(ctx));
    AmiAPI_Destroy(ctx);
    return err == AMIAPI_OK ? 0 : 20;
}
