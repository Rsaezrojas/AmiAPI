#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <amiapi/amiapi.h>
#include <amiapi/json.h>
#include <amiapi/json_http.h>

int main(int argc, char **argv)
{
    struct AmiAPIContext *ctx;
    struct AmiAPIResponse *response;
    struct AmiAPIJsonDoc *doc;
    const struct AmiAPIJsonValue *root;
    const struct AmiAPIJsonValue *value;
    enum AmiAPIJsonError json_error;
    size_t json_offset;
    char buffer[64];
    size_t required;

    if (argc != 2) {
        fprintf(stderr, "usage: %s URL\n", argv[0]);
        return 2;
    }

    ctx = AmiAPI_Create();
    if (!ctx)
        return 3;

    response = AmiAPI_Get(ctx, argv[1]);

    if (!response) {
        fprintf(
            stderr,
            "GET failed: %s\n",
            AmiAPI_ErrorString(AmiAPI_LastError(ctx))
        );
        AmiAPI_Destroy(ctx);
        return 20;
    }

    printf(
        "is-json: %d\n",
        AmiAPI_JSONResponseIsJSON(response)
    );

    json_error = AMIAPI_JSON_OK;
    json_offset = 0;

    doc = AmiAPI_JSONParseResponse(
        response,
        &json_error,
        &json_offset
    );

    if (!doc) {
        fprintf(
            stderr,
            "JSON parse failed: error=%d offset=%lu\n",
            (int)json_error,
            (unsigned long)json_offset
        );

        AmiAPI_FreeResponse(response);
        AmiAPI_Destroy(ctx);
        return 21;
    }

    root = AmiAPI_JSONRoot(doc);
    value = AmiAPI_JSONObjectGet(doc, root, "name");

    if (value) {
        required = 0;

        if (AmiAPI_JSONStringCopy(
                doc,
                value,
                buffer,
                sizeof(buffer),
                &required) == AMIAPI_JSON_OK)
        {
            printf("name: %s\n", buffer);
        }
    }

    AmiAPI_JSONFree(doc);
    AmiAPI_FreeResponse(response);
    AmiAPI_Destroy(ctx);

    return 0;
}
