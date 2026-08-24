#include <stdlib.h>
#include <amiapi/amiapi.h>
#include "internal.h"

struct AmiAPIContext *AmiAPI_Create(void)
{
    struct AmiAPIContext *ctx;

    ctx = (struct AmiAPIContext *)malloc(sizeof(*ctx));
    if (ctx == NULL)
        return NULL;

    ctx->magic = AMIAPI_CONTEXT_MAGIC;
    ctx->max_body = (size_t)AMIAPI_DEFAULT_MAX_BODY;
    ctx->last_error = AMIAPI_OK;
    return ctx;
}

void AmiAPI_Destroy(struct AmiAPIContext *ctx)
{
    if (ctx == NULL)
        return;

    ctx->magic = 0;
    free(ctx);

    /*
     * M1.1 has no shared context registry, so cleanup is conservative and
     * intended for the normal one-context command-line/application case.
     * This will be refined before a shared-library ABI is frozen.
     */
    amiapi_transport_platform_cleanup();
}

const char *AmiAPI_VersionString(void)
{
    return "0.1.1";
}

void AmiAPI_SetMaxBody(struct AmiAPIContext *ctx, size_t max_body)
{
    if (ctx == NULL || ctx->magic != AMIAPI_CONTEXT_MAGIC)
        return;
    ctx->max_body = max_body;
}

enum AmiAPIError AmiAPI_LastError(const struct AmiAPIContext *ctx)
{
    if (ctx == NULL || ctx->magic != AMIAPI_CONTEXT_MAGIC)
        return AMIAPI_ERR_INVALID_ARGUMENT;
    return ctx->last_error;
}

void amiapi_set_error(struct AmiAPIContext *ctx, enum AmiAPIError error)
{
    if (ctx != NULL && ctx->magic == AMIAPI_CONTEXT_MAGIC)
        ctx->last_error = error;
}
