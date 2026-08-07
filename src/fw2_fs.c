#include "fw2_fs.h"
#include "fw2.h"
#include "onewili.h"
#include "onewili_sd.h"
#include "input/app_recovery_onewili.h"
#include <string.h>
#include <stdio.h>

#define FW2_FS_DIR_MAX 128
typedef struct { FIL *fp; ow_sd_file file; FRESULT close_result; bool used; } file_slot_t;
typedef struct { char name[FF_LFN_BUF + 1]; uint32_t size; bool is_dir; } dir_entry_t;
static ow_device s_dev;
static bool s_ready;
static char s_cwd[SDFS_MAX_PATH + 1] = "/";
static file_slot_t s_files[OW_SD_MAX_HANDLES];
static struct { DIR *dp; unsigned index, count; dir_entry_t entries[FW2_FS_DIR_MAX]; } s_dir;

static FRESULT map_status(ow_status s) {
    if (s == OW_OK) return FR_OK;
    if (s == OW_ERR_TIMEOUT) return FR_TIMEOUT;
    if (s == OW_ERR_ARG) return FR_INVALID_NAME;
    return FR_DISK_ERR;
}
static bool abs_path(const char *in, char out[SDFS_MAX_PATH + 1]) {
    if (!in || !*in) return false;
    if (!strcmp(in, ".")) in = s_cwd;
    if (in[0] == '/') {
        if (strlen(in) > SDFS_MAX_PATH) return false;
        strcpy(out, in);
    } else {
        int n = snprintf(out, SDFS_MAX_PATH + 1, !strcmp(s_cwd, "/") ? "/%s" : "%s/%s", s_cwd, in);
        if (n < 0 || n > SDFS_MAX_PATH) return false;
    }
    return true;
}
static file_slot_t *slot_for(FIL *fp) {
    for (unsigned i = 0; i < OW_SD_MAX_HANDLES; ++i)
        if (s_files[i].used && s_files[i].fp == fp) return &s_files[i];
    return NULL;
}
bool fw2_fs_init(void) {
    if (fw2_app_recovery_open_onewili(&s_dev) != OW_OK) return false;
    if (fw2_app_recovery_wrap_sd() != OW_OK) return false;
    ow_sd_set_timeout_ms(4000);
    s_ready = true;
    return true;
}
bool fw2_fs_ready(void) { return s_ready; }
FRESULT f_mount(FATFS *fs, const TCHAR *path, BYTE opt) { (void)fs; (void)path; (void)opt; return s_ready ? FR_OK : FR_NOT_READY; }
FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode) {
    char p[SDFS_MAX_PATH + 1]; bool dir = false; uint32_t size = 0;
    if (!s_ready) return FR_NOT_READY;
    if (!fp || !abs_path(path, p)) return FR_INVALID_NAME;
    file_slot_t *slot = NULL;
    for (unsigned i = 0; i < OW_SD_MAX_HANDLES; ++i) if (!s_files[i].used) { slot = &s_files[i]; break; }
    if (!slot) return FR_TOO_MANY_OPEN_FILES;
    ow_sd_mode om = (mode & FA_WRITE) ? ((mode & FA_OPEN_APPEND) == FA_OPEN_APPEND ? OW_SD_APPEND : OW_SD_WRITE) : OW_SD_READ;
    ow_status st = ow_sd_open(&s_dev, &slot->file, p, om);
    if (st != OW_OK) return map_status(st);
    memset(fp, 0, sizeof *fp);
    if (ow_sd_stat(&s_dev, p, &dir, &size) == OW_OK) fp->obj.objsize = size;
    fp->fptr = (om == OW_SD_APPEND) ? size : 0;
    slot->fp = fp; slot->used = true; slot->close_result = FR_OK;
    return FR_OK;
}
FRESULT f_close(FIL *fp) {
    file_slot_t *slot = slot_for(fp);
    if (!slot) return FR_INVALID_OBJECT;
    if (slot->file.is_open) slot->close_result = map_status(ow_sd_close(&slot->file));
    FRESULT result = slot->close_result;
    slot->used = false;
    return result;
}
FRESULT f_read(FIL *fp, void *buf, UINT len, UINT *got) {
    file_slot_t *slot = slot_for(fp); size_t n = 0;
    if (got) *got = 0;
    if (!slot) return FR_INVALID_OBJECT;
    FRESULT r = map_status(ow_sd_read(&slot->file, buf, len, &n));
    fp->fptr += n; if (got) *got = (UINT)n; return r;
}
FRESULT f_write(FIL *fp, const void *buf, UINT len, UINT *written) {
    file_slot_t *slot = slot_for(fp);
    if (written) *written = 0;
    if (!slot) return FR_INVALID_OBJECT;
    FRESULT r = map_status(ow_sd_write(&slot->file, buf, len));
    if (r == FR_OK) { fp->fptr += len; if (fp->fptr > fp->obj.objsize) fp->obj.objsize = fp->fptr; if (written) *written = len; }
    return r;
}
FRESULT f_lseek(FIL *fp, FSIZE_t offset) {
    file_slot_t *slot = slot_for(fp); if (!slot) return FR_INVALID_OBJECT;
    FRESULT r = map_status(ow_sd_seek(&slot->file, (uint32_t)offset)); if (r == FR_OK) fp->fptr = offset; return r;
}
FRESULT f_sync(FIL *fp) {
    file_slot_t *slot = slot_for(fp); if (!slot) return FR_INVALID_OBJECT;
    if (!slot->file.is_open) return slot->close_result;
    slot->close_result = map_status(ow_sd_close(&slot->file));
    return slot->close_result;
}
static void list_cb(const char *name, bool is_dir, uint32_t size, void *user) {
    (void)user; if (s_dir.count >= FW2_FS_DIR_MAX) return;
    dir_entry_t *e = &s_dir.entries[s_dir.count++];
    strncpy(e->name, name, FF_LFN_BUF); e->name[FF_LFN_BUF] = 0; e->is_dir = is_dir; e->size = size;
}
FRESULT f_opendir(DIR *dp, const TCHAR *path) {
    char p[SDFS_MAX_PATH + 1]; if (!s_ready) return FR_NOT_READY;
    if (!dp || !abs_path(path, p)) return FR_INVALID_NAME;
    s_dir.dp = dp; s_dir.index = s_dir.count = 0;
    return map_status(ow_sd_list(&s_dev, p, list_cb, NULL));
}
FRESULT f_readdir(DIR *dp, FILINFO *fno) {
    if (dp != s_dir.dp || !fno) return FR_INVALID_OBJECT;
    memset(fno, 0, sizeof *fno);
    if (s_dir.index >= s_dir.count) return FR_OK;
    dir_entry_t *e = &s_dir.entries[s_dir.index++];
    strncpy(fno->fname, e->name, FF_LFN_BUF); fno->fsize = e->size; fno->fattrib = e->is_dir ? AM_DIR : AM_ARC;
    return FR_OK;
}
FRESULT f_closedir(DIR *dp) { if (dp != s_dir.dp) return FR_INVALID_OBJECT; s_dir.dp = NULL; return FR_OK; }
FRESULT f_stat(const TCHAR *path, FILINFO *fno) {
    char p[SDFS_MAX_PATH + 1]; bool dir = false; uint32_t size = 0;
    if (!abs_path(path, p)) return FR_INVALID_NAME;
    FRESULT r = map_status(ow_sd_stat(&s_dev, p, &dir, &size));
    if (r == FR_OK && fno) { memset(fno, 0, sizeof *fno); fno->fsize = size; fno->fattrib = dir ? AM_DIR : AM_ARC; const char *n = strrchr(p, '/'); strncpy(fno->fname, n ? n + 1 : p, FF_LFN_BUF); }
    return r;
}
FRESULT f_mkdir(const TCHAR *path) { char p[SDFS_MAX_PATH + 1]; return abs_path(path,p) ? map_status(ow_sd_mkdir(&s_dev,p)) : FR_INVALID_NAME; }
FRESULT f_unlink(const TCHAR *path) { char p[SDFS_MAX_PATH + 1]; return abs_path(path,p) ? map_status(ow_sd_remove(&s_dev,p)) : FR_INVALID_NAME; }
FRESULT f_rename(const TCHAR *a, const TCHAR *b) { char pa[SDFS_MAX_PATH+1], pb[SDFS_MAX_PATH+1]; return abs_path(a,pa)&&abs_path(b,pb) ? map_status(ow_sd_rename(&s_dev,pa,pb)) : FR_INVALID_NAME; }
FRESULT f_chdir(const TCHAR *path) {
    char p[SDFS_MAX_PATH + 1]; bool dir = false; uint32_t size;
    if (!abs_path(path,p) || ow_sd_stat(&s_dev,p,&dir,&size) != OW_OK || !dir) return FR_NO_PATH;
    strcpy(s_cwd,p); return FR_OK;
}
FRESULT f_getcwd(TCHAR *buf, UINT len) { if (!buf || len <= strlen(s_cwd)) return FR_INVALID_PARAMETER; strcpy(buf,s_cwd); return FR_OK; }