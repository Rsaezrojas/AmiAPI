#include <stdlib.h>

#include <amiapi/amiapi.h>

struct AmiAPIContext {
    unsigned long magic;
};

#define AMIAPI_CONTEXT_MAGIC 0x41415049UL

struct AmiAPIContext *AmiAPI_Create(void)
{
    struct AmiAPIContext *ctx;

    ctx = (struct AmiAPIContext *)malloc(sizeof(*ctx));
    if (ctx == NULL)
        return NULL;

    ctx->magic = AMIAPI_CONTEXT_MAGIC;
    return ctx;
}

void AmiAPI_Destroy(struct AmiAPIContext *ctx)
{
    if (ctx == NULL)
        return;

    ctx->magic = 0;
    free(ctx);
}

const char *AmiAPI_VersionString(void)
{
    return "0.0.1";
}
