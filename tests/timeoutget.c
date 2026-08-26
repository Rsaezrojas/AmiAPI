#include <stdio.h>
#include <stdlib.h>

#include <amiapi/amiapi.h>

int main(int argc, char **argv)
{
    struct AmiAPIContext *ctx;
    struct AmiAPIResponse *response;
    unsigned long seconds;
    char *end;

    if (argc != 3) {
        fprintf(
            stderr,
            "usage: %s URL SECONDS\n",
            argv[0]
        );
        return 2;
    }

    seconds = strtoul(argv[2], &end, 10);

    if (*argv[2] == '\0' ||
        *end != '\0' ||
        seconds > 0xffffffffUL)
    {
        return 2;
    }

    ctx = AmiAPI_Create();
    if (!ctx)
        return 3;

    AmiAPI_SetTimeout(
        ctx,
        (unsigned int)seconds
    );

    if (AmiAPI_Timeout(ctx) != (unsigned int)seconds) {
        fprintf(stderr, "timeout getter mismatch\n");
        AmiAPI_Destroy(ctx);
        return 4;
    }

    response = AmiAPI_Get(
        ctx,
        argv[1]
    );

    if (!response) {
        fprintf(
            stderr,
            "GET failed: %s\n",
            AmiAPI_ErrorString(AmiAPI_LastError(ctx))
        );

        AmiAPI_Destroy(ctx);
        return 20;
    }

    printf("HTTP %d\n", AmiAPI_Status(response));

    AmiAPI_FreeResponse(response);
    AmiAPI_Destroy(ctx);

    return 0;
}
