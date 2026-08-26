#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <amiapi/amiapi.h>

struct DownloadProgress {
    size_t received;
    size_t total;
    unsigned long calls;
    unsigned long cancel_call;
};

static int report_progress(size_t received, size_t total, void *userdata)
{
    struct DownloadProgress *progress =
        (struct DownloadProgress *)userdata;

    progress->received = received;
    progress->total = total;
    progress->calls++;
    return !progress->cancel_call ||
        progress->calls < progress->cancel_call;
}

int main(int argc, char **argv)
{
    struct AmiAPIContext *ctx;
    struct AmiAPIRequest *request;
    enum AmiAPIError err;
    struct DownloadProgress progress;
    int i;

    if (argc < 7 || (argc - 7) % 2 != 0) {
        fprintf(stderr,
            "usage: %s METHOD URL PATH MAX_BODY TIMEOUT CANCEL_CALL [NAME VALUE ...]\n",
            argv[0]);
        return 2;
    }

    progress.received = 0;
    progress.total = 0;
    progress.calls = 0;
    progress.cancel_call = strtoul(argv[6], NULL, 10);

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

        err = AmiAPI_RequestSetProgressCallback(
            request,
            report_progress,
            &progress
        );
        if (err != AMIAPI_OK) {
            AmiAPI_RequestDestroy(request);
            AmiAPI_Destroy(ctx);
            return 5;
        }

        for (i = 7; i < argc; i += 2) {
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
    printf("progress-calls: %lu\n", progress.calls);
    printf("received: %lu\n", (unsigned long)progress.received);
    printf("total: %lu\n", (unsigned long)progress.total);
    AmiAPI_Destroy(ctx);
    return err == AMIAPI_OK ? 0 : 20;
}
