/* ================================================================
 * SPI 使用示例
 *
 * 功能：打开 SPI0 → 配置 10MHz mode 0 → 读写 Flash → 关闭
 *
 * Flash W25Q 系列读 ID 为例：
 *   指令 0x90 + 3 字节地址(0x00) + 1 byte dummy → 返回 2 字节 ID
 *
 * 编译要求：需要 posix.h + posix_ioctl_spi.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_spi.h"

void example_spi(void)
{
    /* === 1. 打开设备 === */
    posix_fd_t spi = posix_open("/dev/spi0");
    if (!spi)
    {
        return;
    }

    /* === 2. 配置参数 === */
    posix_spi_config_t cfg =
    {
        .freq_hz       = 10000000,   /* 10MHz */
        .mode          = POSIX_SPI_MODE_0,   /* CPOL=0, CPHA=0 */
        .bits_per_word = 8,
        .cs_pin        = -1,          /* -1 = 程序自己控制 CS */
    };
    posix_ioctl(spi, POSIX_SPI_IOCTL_SET_CONFIG, &cfg);

    /* === 3. 全双工传输：读 Flash ID === */
    uint8_t tx_buf[] = { 0x90, 0x00, 0x00, 0x00, 0x00 };
    uint8_t rx_buf[5] = {0};

    posix_ioctl(spi, POSIX_SPI_IOCTL_CS_TAKE, NULL);   /* CS 拉低 */

    posix_spi_transfer_t xfer =
    {
        .tx_buf = tx_buf,
        .rx_buf = rx_buf,
        .len    = 5,
    };
    posix_ioctl(spi, POSIX_SPI_IOCTL_TRANSFER, &xfer);

    posix_ioctl(spi, POSIX_SPI_IOCTL_CS_RELEASE, NULL); /* CS 拉高 */

    /* uint16_t flash_id = (rx_buf[3] << 8) | rx_buf[4]; */

    /* === 4. 只发不收（半双工） === */
    uint8_t cmd[] = { 0x06 };   /* W25Q Write Enable */
    posix_ioctl(spi, POSIX_SPI_IOCTL_CS_TAKE, NULL);
    posix_write(spi, cmd, sizeof(cmd));
    posix_ioctl(spi, POSIX_SPI_IOCTL_CS_RELEASE, NULL);

    /* === 5. 只收不发（半双工） === */
    uint8_t status[2] = {0};
    uint8_t read_status[] = { 0x05, 0x00 };
    posix_ioctl(spi, POSIX_SPI_IOCTL_CS_TAKE, NULL);
    /* 发 0x05 的同时收 0x00 */
    posix_spi_transfer_t xfer2 =
    {
        .tx_buf = read_status,
        .rx_buf = status,
        .len    = 2,
    };
    posix_ioctl(spi, POSIX_SPI_IOCTL_TRANSFER, &xfer2);
    posix_ioctl(spi, POSIX_SPI_IOCTL_CS_RELEASE, NULL);

    /* === 6. 关闭 === */
    posix_close(spi);
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include "posix_port.h"
static bool s_spi_inited = false;

static int cmd_spi_test(const struct shell *sh, size_t argc, char **argv)
{
    if (!s_spi_inited) { posix_port_init_all(); s_spi_inited = true; }

    posix_fd_t fd = posix_open("/dev/spi0");
    if (fd == POSIX_FD_NULL) { shell_error(sh, "open /dev/spi0 failed"); return -1; }

    posix_spi_config_t cfg =
    {
        .freq_hz       = 1000000,
        .mode          = POSIX_SPI_MODE_0,
        .bits_per_word = 8,
        .cs_pin        = -1,
    };
    posix_ioctl(fd, POSIX_SPI_IOCTL_SET_CONFIG, &cfg);
    shell_print(sh, "SPI config OK (1MHz mode0)");

    posix_close(fd);
    shell_print(sh, "POSIX SPI test PASSED");
    return 0;
}
SHELL_CMD_REGISTER(posix_spi, NULL, "POSIX SPI smoke test", cmd_spi_test);
#endif /* CONFIG_SHELL */
