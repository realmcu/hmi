/* ================================================================
 * SDIO 使用示例
 *
 * 功能：打开 SDIO0 → 获取卡信息 → 读 MBR → 写数据 → 关闭
 *
 * SDIO 使用块设备语义（类似 Linux /dev/mmcblk0）：
 *   块大小 = 512 字节，读写按块对齐
 *
 * 编译要求：需要 posix.h + posix_ioctl_sdio.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_sdio.h"

void example_sdio(void)
{
    /* === 1. 打开设备 === */
    posix_fd_t sd = posix_open("/dev/sdio0");
    if (!sd)
    {
        return;
    }

    /* === 2. 获取 SD 卡信息 === */
    posix_sdio_info_t info;
    posix_ioctl(sd, POSIX_SDIO_IOCTL_GET_INFO, &info);

    /* info.block_count  = 总块数（如 15360000）
     * info.block_size   = 每块字节数（通常 512）
     * info.capacity_kb  = 总容量 KB
     * info.is_mmc       = 0=SD卡, 1=eMMC */

    /* === 3. 读块 0（MBR/分区表） === */
    uint8_t mbr[512];
    posix_ioctl(sd, POSIX_SDIO_IOCTL_READ_BLOCKS,
                &(posix_sdio_block_t)
    {
        .block_addr  = 0,
         .block_count = 1,
          .data        = mbr,
    });

    /* === 4. 写块（注意：写之前确保块已擦除） === */
    uint8_t write_data[512] = { 0 };
    /* memcpy(write_data, "HELLO", 5); */
    posix_ioctl(sd, POSIX_SDIO_IOCTL_WRITE_BLOCKS,
                &(posix_sdio_block_t)
    {
        .block_addr  = 1024,
         .block_count = 1,
          .data        = write_data,
    });

    /* === 5. 配置总线（切换 4bit 高速模式） === */
    posix_sdio_config_t cfg =
    {
        .bus_width   = POSIX_SDIO_BUS_4BIT,
        .speed_mode  = POSIX_SDIO_SPEED_HIGH,
        .max_freq_hz = 50000000,
    };
    posix_ioctl(sd, POSIX_SDIO_IOCTL_SET_CONFIG, &cfg);

    /* === 6. 关闭 === */
    posix_close(sd);
}
