#include <stdio.h>
#include <stdlib.h>

#include <amiapi/amiapi.h>
#include <amiapi/errors.h>

int main(int argc, char **argv)
{
    struct AmiAPIContext *ctx;
    struct AmiAPIResponse *response;
    const char *content_type;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s http://host/path\n", argv[0]);
        return 10;
    }

    ctx = AmiAPI_Create();
    if (ctx == NULL) {
        fprintf(stderr, "Unable to create AmiAPI context\n");
        return 20;
    }

    response = AmiAPI_Get(ctx, argv[1]);
    if (response == NULL) {
        enum AmiAPIError error = AmiAPI_LastError(ctx);
        fprintf(stderr, "GET failed: %s\n", AmiAPI_ErrorString(error));
        AmiAPI_Destroy(ctx);
        return 20;
    }

    printf("HTTP %d\n", AmiAPI_Status(response));
    content_type = AmiAPI_Header(response, "Content-Type");
    if (content_type != NULL)
        printf("content-type: %s\n", content_type);
    printf("body-bytes: %lu\n", (unsigned long)AmiAPI_BodySize(response));

    AmiAPI_FreeResponse(response);
    AmiAPI_Destroy(ctx);
    return 0;
}
