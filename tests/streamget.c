#include <stdio.h>
#include <stdlib.h>

#include <amiapi/amiapi.h>

struct StreamState {
    size_t bytes;
    unsigned long calls;
    unsigned long fail_call;
};

static enum AmiAPIError receive_body(
    const void *data,
    size_t size,
    void *userdata)
{
    const unsigned char *bytes = (const unsigned char *)data;
    struct StreamState *state = (struct StreamState *)userdata;
    size_t i;

    state->calls++;
    if (state->fail_call && state->calls >= state->fail_call)
        return AMIAPI_ERR_INTERNAL;

    for (i = 0; i < size; ++i)
        printf("%02x", (unsigned int)bytes[i]);

    state->bytes += size;
    return AMIAPI_OK;
}

int main(int argc, char **argv)
{
    struct AmiAPIContext *ctx;
    struct AmiAPIRequest *request;
    struct AmiAPIResponse *response;
    struct StreamState state;
    enum AmiAPIError err;
    size_t max_body;

    if (argc < 3 || argc > 5) {
        fprintf(stderr, "usage: %s METHOD URL [MAX_BODY [FAIL_CALL]]\n", argv[0]);
        return 2;
    }

    state.bytes = 0;
    state.calls = 0;
    state.fail_call = argc == 5 ? strtoul(argv[4], NULL, 10) : 0;
    max_body = argc >= 4 ? (size_t)strtoul(argv[3], NULL, 10) : AMIAPI_DEFAULT_MAX_BODY;

    ctx = AmiAPI_Create();
    if (!ctx)
        return 3;
    AmiAPI_SetMaxBody(ctx, max_body);

    request = AmiAPI_RequestCreate(ctx, argv[1], argv[2]);
    if (!request) {
        AmiAPI_Destroy(ctx);
        return 4;
    }

    err = AmiAPI_RequestSetResponseCallback(request, receive_body, &state);
    if (err != AMIAPI_OK) {
        AmiAPI_RequestDestroy(request);
        AmiAPI_Destroy(ctx);
        return 5;
    }

    response = AmiAPI_RequestExecute(request);
    if (!response) {
        fprintf(stderr, "error: %d\n", (int)AmiAPI_LastError(ctx));
        printf("\ncallback-bytes: %lu\ncallback-calls: %lu\n",
            (unsigned long)state.bytes, state.calls);
        AmiAPI_RequestDestroy(request);
        AmiAPI_Destroy(ctx);
        return 20;
    }

    printf("\nHTTP %d\ncallback-bytes: %lu\ncallback-calls: %lu\n",
        AmiAPI_Status(response), (unsigned long)state.bytes, state.calls);
    printf("response-body-null: %d\nresponse-body-size: %lu\n",
        AmiAPI_Body(response) == NULL, (unsigned long)AmiAPI_BodySize(response));
    printf("x-stream-test: %s\n",
        AmiAPI_Header(response, "X-Stream-Test")
            ? AmiAPI_Header(response, "X-Stream-Test")
            : "(none)");

    AmiAPI_FreeResponse(response);
    AmiAPI_RequestDestroy(request);
    AmiAPI_Destroy(ctx);
    return 0;
}
