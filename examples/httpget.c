#include <stdio.h>
#include <amiapi/amiapi.h>
#include <amiapi/errors.h>
int main(int argc, char **argv)
{
    struct AmiAPIContext *ctx; struct AmiAPIResponse *r; const char *ct;
    if (argc != 2) { fprintf(stderr, "Usage: %s http://host/path\n", argv[0]); return 10; }
    ctx = AmiAPI_Create(); if (!ctx) return 20;
    r = AmiAPI_Get(ctx, argv[1]);
    if (!r) { enum AmiAPIError e = AmiAPI_LastError(ctx); fprintf(stderr, "GET failed: %s\n", AmiAPI_ErrorString(e)); AmiAPI_Destroy(ctx); return 20; }
    printf("HTTP %d\n", AmiAPI_Status(r));
    ct = AmiAPI_Header(r, "Content-Type"); if (ct) printf("content-type: %s\n", ct);
    printf("body-bytes: %lu\n", (unsigned long)AmiAPI_BodySize(r));
    AmiAPI_FreeResponse(r); AmiAPI_Destroy(ctx); return 0;
}
