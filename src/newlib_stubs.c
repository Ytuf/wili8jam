#include <errno.h>

int _unlink(const char *path) {
    (void)path;
    errno = ENOSYS;
    return -1;
}

int _link(const char *old_path, const char *new_path) {
    (void)old_path;
    (void)new_path;
    errno = ENOSYS;
    return -1;
}
