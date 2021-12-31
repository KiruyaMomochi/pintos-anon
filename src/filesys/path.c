#include <string.h>

#include "directory.h"
#include "path.h"
#include "userprog/process.h"

/* Returns if PATH is a absolute path. */
bool
path_is_absolute (const char *path)
{
  ASSERT (path != NULL);

  if (strlen (path) == 0)
    return false;
  return path[0] == PATH_SEPARATOR;
}

/* Combines PATH1 and PATH2 and writes the result to DEST.
   If combined path is longer than SIZE - 1, it is truncated. */
void
path_combine (char *dest, const char *path1, const char *path2, size_t size)
{
  ASSERT (dest != NULL);
  ASSERT (path1 != NULL);
  ASSERT (path2 != NULL);
  ASSERT (size > 0);

  if (path_is_absolute (path2))
    strlcpy (dest, path2, size);
  else
    {
      strlcpy (dest, path1, size);
      size_t len = strlen (dest);
      if (dest[len - 1] != PATH_SEPARATOR)
        {
          strlcat (dest, PATH_SEPARATOR_STR, size);
          len++;
        }
      strlcat (dest, path2, size - len);
    }
}

/* Split PATH into parent and base parts.
   PARENT_LENGTH is the length of parent part.
   Base part starts at BASE_BEGIN, ends at BASE_END.

   If path is not splitable, PARENT_LENGTH is set to 0.

   Example:
    "/a/b/c" -> parent = "/a/b", base = "c"
    "a/b/c/" -> parent =  "a/b", base = "c"
    "a///b/" -> parent =    "a", base = "b"
    "/a"     -> parent =    "/", base = "a"
    "/"      -> parent =     "", base = "/"
    "a"      -> parent =     "", base = "a" */
void
path_split (const char *path, size_t *parent_length, const char **base_begin,
            const char **base_end)
{
  ASSERT (path != NULL);
  size_t path_len = strlen (path);

  /* Set default values, returned if path is not splitable. */
  *parent_length = 0;
  *base_begin = path;
  *base_end = path;
  *base_end = path + path_len;

  /* Stop for empty path. */
  if (path_len == 0)
    return;

  /* Set ch to last character of path. */
  const char *ch = path + path_len - 1;

  /* Matches the last character of base name. */
  while (*ch == PATH_SEPARATOR)
    {
      if (ch == path)
        return;
      ch--;
    }
  *base_end = ch + 1;

  /* Matches the last slash before base name. */
  while (*ch != PATH_SEPARATOR)
    {
      if (ch == path)
        return;
      ch--;
    }
  *base_begin = ch + 1;

  /* Matches the first slash before base name. */
  while (*ch == PATH_SEPARATOR)
    {
      if (ch == path)
        {
          /* The path is a root directory. */
          *parent_length = 1;
          return;
        }

      ch--;
    }
  *parent_length = ch - path + 1;
}
