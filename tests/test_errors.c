#include <stdio.h>

#include <amiapi/errors.h>

int main(void)
{
    int error;

    for (error = (int)AMIAPI_OK;
         error <= (int)AMIAPI_ERR_CANCELLED;
         ++error)
    {
        const char *text = AmiAPI_ErrorString((enum AmiAPIError)error);
        if (!text || !*text) {
            fprintf(stderr, "missing error string for %d\n", error);
            return 1;
        }
    }

    puts("Error string tests: OK");
    return 0;
}
