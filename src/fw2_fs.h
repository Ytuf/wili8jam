#ifndef FW2_FS_H
#define FW2_FS_H
#include "fatfs/ff.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool fw2_fs_init(void);
bool fw2_fs_ready(void);
#ifdef __cplusplus
}
#endif
#endif