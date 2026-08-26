#include <stdio.h>

#include "internal.h"

int amiapi_file_publish(const char *temporary_path, const char *path)
{
    return rename(temporary_path, path);
}

void amiapi_file_remove(const char *path)
{
    remove(path);
}
