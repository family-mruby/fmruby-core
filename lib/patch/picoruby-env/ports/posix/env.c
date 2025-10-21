#include <stddef.h>
#include <string.h>
#include <stdlib.h>

extern char **environ;
static __thread char **my_environ;
static __thread int first = 1;

void
ENV_get_key_value(char **key, char **value)
{
  if (first) {
    my_environ = environ;
    first = 0;
  }

  if (my_environ == NULL || *my_environ == NULL) {
    *key = NULL;
    *value = NULL;
    return;
  }

  char *saveptr;
  *key = strtok_r(*my_environ, "=", &saveptr);
  *value = strtok_r(NULL, "=", &saveptr);

  my_environ++;
}

int ENV_unsetenv(const char *name)
{
  return unsetenv(name);
}

int ENV_setenv(const char *name, const char *value, int override)
{
  return setenv(name, value, override);
}
