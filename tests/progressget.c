#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <amiapi/amiapi.h>

struct ProgressState {
    size_t received;
    size_t total;
    size_t streamed;
    unsigned long calls;
    unsigned long cancel_call;
    int fail_body;
};

static int report_progress(size_t received, size_t total, void *userdata)
{
    struct ProgressState *state = (struct ProgressState *)userdata;

    state->received = received;
    state->total = total;
    state->calls++;
    return !state->cancel_call || state->calls < state->cancel_call;
}

static enum AmiAPIError receive_body(
    const void *data,
    size_t size,
    void *userdata)
{
    struct ProgressState *state = (struct ProgressState *)userdata;

    if (!data && size != 0)
        return AMIAPI_ERR_INTERNAL;
    if (state->fail_body)
        return AMIAPI_ERR_INTERNAL;
    state->streamed += size;
    return AMIAPI_OK;
}

static void print_header(
    const struct AmiAPIResponse *response,
    const char *name)
{
    const char *value = AmiAPI_Header(response, name);
    printf("%s: %s\n", name, value ? value : "(null)");
}

int main(int argc, char **argv)
{
    struct AmiAPIContext *ctx;
    struct AmiAPIRequest *request;
    struct AmiAPIResponse *response;
    struct ProgressState state;
    enum AmiAPIError err;

    if (argc != 5) {
        fprintf(stderr, "usage: %s METHOD MODE URL CANCEL_CALL\n", argv[0]);
        return 2;
    }

    memset(&state, 0, sizeof(state));
    state.cancel_call = strtoul(argv[4], NULL, 10);
    state.fail_body = strcmp(argv[2], "failstream") == 0;
    ctx = AmiAPI_Create();
    if (!ctx)
        return 3;
    request = AmiAPI_RequestCreate(ctx, argv[1], argv[3]);
    if (!request) {
        AmiAPI_Destroy(ctx);
        return 4;
    }

    err = AmiAPI_RequestSetProgressCallback(request, report_progress, &state);
    if (err == AMIAPI_OK &&
        (strcmp(argv[2], "stream") == 0 || state.fail_body))
        err = AmiAPI_RequestSetResponseCallback(request, receive_body, &state);
    if (err != AMIAPI_OK) {
        AmiAPI_RequestDestroy(request);
        AmiAPI_Destroy(ctx);
        return 5;
    }

    response = AmiAPI_RequestExecute(request);
    printf("progress-calls: %lu\n", state.calls);
    printf("received: %lu\n", (unsigned long)state.received);
    printf("total: %lu\n", (unsigned long)state.total);
    printf("streamed: %lu\n", (unsigned long)state.streamed);
    if (!response) {
        printf("last-error: %d\n", (int)AmiAPI_LastError(ctx));
        AmiAPI_RequestDestroy(request);
        AmiAPI_Destroy(ctx);
        return 20;
    }

    printf("HTTP %d\n", AmiAPI_Status(response));
    printf("body-size: %lu\n", (unsigned long)AmiAPI_BodySize(response));
    print_header(response, "Content-Type");
    print_header(response, "content-type");
    print_header(response, "X-Duplicate");
    print_header(response, "X-Final");
    print_header(response, "X-Intermediate");
    print_header(response, "X-Missing");
    printf("invalid-header-args: %d\n",
        AmiAPI_Header(NULL, "X") == NULL &&
        AmiAPI_Header(response, NULL) == NULL);

    AmiAPI_FreeResponse(response);
    AmiAPI_RequestDestroy(request);
    AmiAPI_Destroy(ctx);
    return 0;
}
