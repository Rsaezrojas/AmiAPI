#include <stdio.h>

#include <amiapi/amiapi.h>
#include <amiapi/form.h>

int main(int argc, char **argv)
{
    struct AmiAPIContext *ctx;
    struct AmiAPIRequest *request;
    struct AmiAPIResponse *response;
    struct AmiAPIForm *form;
    enum AmiAPIError err;

    if (argc != 2) {
        fprintf(
            stderr,
            "usage: %s URL\n",
            argv[0]
        );
        return 2;
    }

    ctx = AmiAPI_Create();
    if (!ctx)
        return 3;

    request = AmiAPI_RequestCreate(
        ctx,
        "POST",
        argv[1]
    );

    if (!request) {
        AmiAPI_Destroy(ctx);
        return 4;
    }

    form = AmiAPI_FormCreate();

    if (!form) {
        AmiAPI_RequestDestroy(request);
        AmiAPI_Destroy(ctx);
        return 5;
    }

    err = AmiAPI_FormAdd(
        form,
        "grant_type",
        "authorization code"
    );

    if (err == AMIAPI_OK) {
        err = AmiAPI_FormAdd(
            form,
            "special",
            "a+b&c=d"
        );
    }

    if (err == AMIAPI_OK) {
        err = AmiAPI_RequestSetForm(
            request,
            form
        );
    }

    /*
     * Prove RequestSetForm() copies the body into the request.
     */
    AmiAPI_FormDestroy(form);

    if (err != AMIAPI_OK) {
        fprintf(
            stderr,
            "form setup failed: %s\n",
            AmiAPI_ErrorString(err)
        );

        AmiAPI_RequestDestroy(request);
        AmiAPI_Destroy(ctx);
        return 6;
    }

    response = AmiAPI_RequestExecute(request);

    if (!response) {
        fprintf(
            stderr,
            "request failed: %s\n",
            AmiAPI_ErrorString(AmiAPI_LastError(ctx))
        );

        AmiAPI_RequestDestroy(request);
        AmiAPI_Destroy(ctx);
        return 20;
    }

    printf("HTTP %d\n", AmiAPI_Status(response));

    AmiAPI_FreeResponse(response);
    AmiAPI_RequestDestroy(request);
    AmiAPI_Destroy(ctx);

    return 0;
}
