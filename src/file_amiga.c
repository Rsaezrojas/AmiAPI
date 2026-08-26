#include <proto/dos.h>

#include "internal.h"

int amiapi_file_publish(const char *temporary_path, const char *path)
{
    return Rename((STRPTR)temporary_path, (STRPTR)path) ? 0 : -1;
}

void amiapi_file_remove(const char *path)
{
    DeleteFile((STRPTR)path);
}
