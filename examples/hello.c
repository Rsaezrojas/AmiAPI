#include <stdio.h>

#include <amiapi/amiapi.h>

int main(void)
{
    struct AmiAPIContext *ctx;

    ctx = AmiAPI_Create();
    if (ctx == NULL) {
        fprintf(stderr, "Unable to create AmiAPI context\n");
        return 20;
    }

    printf("AmiAPI %s\n", AmiAPI_VersionString());

    AmiAPI_Destroy(ctx);
    return 0;
}
