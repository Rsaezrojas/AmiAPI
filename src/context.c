#include <stdlib.h>
#include <amiapi/amiapi.h>
#include "internal.h"
static unsigned long amiapi_context_count = 0;
struct AmiAPIContext *AmiAPI_Create(void)
{
    struct AmiAPIContext *ctx = (struct AmiAPIContext *)malloc(sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->magic = AMIAPI_CONTEXT_MAGIC;
    ctx->max_body = (size_t)AMIAPI_DEFAULT_MAX_BODY;
    ctx->last_error = AMIAPI_OK;
    amiapi_context_count++;
    return ctx;
}
void AmiAPI_Destroy(struct AmiAPIContext *ctx)
{
    if (!ctx) return;
    ctx->magic = 0; free(ctx);
    if (amiapi_context_count > 0) amiapi_context_count--;
    if (amiapi_context_count == 0) amiapi_transport_platform_cleanup();
}
const char *AmiAPI_VersionString(void) { return "0.1.2"; }
void AmiAPI_SetMaxBody(struct AmiAPIContext *ctx, size_t n)
{ if (ctx && ctx->magic == AMIAPI_CONTEXT_MAGIC) ctx->max_body = n; }
enum AmiAPIError AmiAPI_LastError(const struct AmiAPIContext *ctx)
{ return (!ctx || ctx->magic != AMIAPI_CONTEXT_MAGIC) ? AMIAPI_ERR_INVALID_ARGUMENT : ctx->last_error; }
void amiapi_set_error(struct AmiAPIContext *ctx, enum AmiAPIError e)
{ if (ctx && ctx->magic == AMIAPI_CONTEXT_MAGIC) ctx->last_error = e; }
