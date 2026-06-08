#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_sdio.h"

typedef struct { int unit; uintptr_t reg_base; } sdio_drv_t;
typedef struct { sdio_drv_t *drv; posix_sdio_config_t cfg; } sdio_file_t;

#define MAX_SDIO_FILES  2
static sdio_file_t s_sdio_files[MAX_SDIO_FILES];
static int s_sdio_file_used[MAX_SDIO_FILES];

static void *sdio_open(void *d, const char *p)
{
    (void)p;
    sdio_file_t *f = NULL;
    for (int i = 0; i < MAX_SDIO_FILES; i++)
    {
        if (!s_sdio_file_used[i]) { s_sdio_file_used[i] = 1; f = &s_sdio_files[i]; break; }
    }
    if (!f) { return POSIX_OPEN_ERR; }
    f->drv = (sdio_drv_t *)d; return f;
}
static int sdio_close(void *d, void *fv)
{
    (void)d;
    sdio_file_t *f = (sdio_file_t *)fv;
    int idx = f - s_sdio_files;
    if (idx >= 0 && idx < MAX_SDIO_FILES) { s_sdio_file_used[idx] = 0; }
    return 0;
}

/* 提供 posix_read/posix_write 的块设备风格 API */
static posix_ssize_t sdio_read(void *d, void *f, void *buf, size_t count)
{
    sdio_drv_t *drv = (sdio_drv_t *)d;
    /* 最简单实现：读第一个块 */
    /* hw_sdio_read_blocks(drv->reg_base, 0, 1, buf); */
    (void)drv; (void)buf; (void)count; (void)f;
    return POSIX_ERR_NOSUPP;
}
static posix_ssize_t sdio_write(void *d, void *f, const void *buf, size_t count)
{
    sdio_drv_t *drv = (sdio_drv_t *)d;
    (void)drv; (void)buf; (void)count; (void)f;
    return POSIX_ERR_NOSUPP;
}

static int sdio_ioctl(void *d, void *f, unsigned long cmd, void *arg)
{
    sdio_drv_t *drv = (sdio_drv_t *)d;
    (void)f;
    switch (cmd)
    {
    case POSIX_SDIO_IOCTL_SET_CONFIG:
        /* hw_sdio_config(drv->reg_base, cfg->bus_width, cfg->speed_mode, cfg->max_freq_hz); */
        return 0;
    case POSIX_SDIO_IOCTL_GET_INFO:
        {
            posix_sdio_info_t *info = (posix_sdio_info_t *)arg;
            /* info->block_count = ... */
            /* info->block_size = 512; */
            /* info->capacity_kb = ... */
            (void)info; return POSIX_ERR_NOSUPP;
        }
    case POSIX_SDIO_IOCTL_READ_BLOCKS:
        {
            posix_sdio_block_t *b = (posix_sdio_block_t *)arg;
            /* hw_sdio_read_blocks(drv->reg_base, b->block_addr, b->block_count, b->data); */
            (void)b; return POSIX_ERR_NOSUPP;
        }
    case POSIX_SDIO_IOCTL_WRITE_BLOCKS:
        {
            posix_sdio_block_t *b = (posix_sdio_block_t *)arg;
            /* hw_sdio_write_blocks(drv->reg_base, b->block_addr, b->block_count, b->data); */
            (void)b; return POSIX_ERR_NOSUPP;
        }
    default: return POSIX_ERR_NOSUPP;
    }
}

const posix_driver_ops_t g_sdio_ops =
{
    .open = sdio_open, .close = sdio_close, .read = sdio_read,
    .write = sdio_write, .ioctl = sdio_ioctl,
};
static sdio_drv_t s_sdio0 = { .unit = 0, .reg_base = 0x40050000 };

/* ---------- 自动注册 ---------- */
static int sdio_init(void)
{
    return posix_device_register("/dev/sdio0", &g_sdio_ops, &s_sdio0);
}
POSIX_INIT_DEVICE_EXPORT(sdio_init);
