/*
 * Self-contained function to try to fetch a single string value from
 * the Registry, and return it as a dynamically allocated C string.
 */

#include "putty.h"

char *registry_get_string(HKEY root, const char *path, const char *leaf)
{
    HKEY key = root;
    bool need_close_key = false;
    char *toret = NULL, *str = NULL;

    if (path) {
        if (RegCreateKey(key, path, &key) != ERROR_SUCCESS)
            goto out;
        need_close_key = true;
    }

    DWORD type, size;
    if (RegQueryValueEx(key, leaf, 0, &type, NULL, &size) != ERROR_SUCCESS)
        goto out;
    if (type != REG_SZ)
        goto out;

    str = snewn(size + 1, char);
    DWORD size_got = size;
    if (RegQueryValueEx(key, leaf, 0, &type, (LPBYTE)str,
                        &size_got) != ERROR_SUCCESS)
        goto out;
    if (type != REG_SZ || size_got > size)
        goto out;
    str[size_got] = '\0';

    toret = str;
    str = NULL;

  out:
    if (need_close_key)
        RegCloseKey(key);
    sfree(str);
    return toret;
}
