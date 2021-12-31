#ifndef FILESYS_PATH_H
#define FILESYS_PATH_H

/* Path utilities. */

#include <string.h>

#define PATH_SEPARATOR '/'
#define PATH_SEPARATOR_STR "/"

size_t path_size_not_separator (const char *path);
bool path_is_absolute (const char *path);
void path_combine (char *dest, const char *path1, const char *path2,
                   size_t size);
void path_split (const char *path, size_t *parent_length,
                 const char **base_begin, const char **base_end);

#endif // FILESYS_PATH_H
