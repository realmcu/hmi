/* ================================================================
 * UART 使用示例
 *
 * 功能：打开 UART0 → 配置 115200-8N1 → 发送数据 → 接收数据
 *       → 注册中断接收回调 → 关闭
 *
 * 编译要求：需要 posix.h + posix_ioctl_uart.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_uart.h"
#include <string.h>

/* 接收中断回调函数 */
static void on_uart_rx(posix_fd_t fd, uint8_t byte, void *arg)
{
    /* ISR 上下文！不能调阻塞函数 */
    /* 通常把数据放入环形缓冲区 */
    (void)fd;
    (void)arg;
}

void example_uart(void)
{
    /* === 1. 打开设备 === */
    posix_fd_t uart = posix_open("/dev/uart0");
    if (!uart)
    {
        /* 设备不存在或注册失败 */
        return;
    }

    /* === 2. 配置参数 === */
    posix_uart_config_t cfg =
    {
        .baudrate     = 115200,
        .data_bits    = 8,
        .parity       = 0,          /* 0=None, 1=Odd, 2=Even */
        .stop_bits    = 1,
        .flow_control = 0,          /* 0=None, 1=RTS/CTS */
    };

    int ret = posix_ioctl(uart, POSIX_UART_IOCTL_SET_CONFIG, &cfg);
    if (ret != POSIX_OK)
    {
        posix_close(uart);
        return;
    }

    /* === 3. 发送（阻塞） === */
    const char *msg = "Hello UART!\r\n";
    posix_write(uart, msg, strlen(msg));

    /* === 4. 接收（非阻塞轮询：FIFO 为空时立即返回 0） === */
    uint8_t buf[64];
    int n = posix_read(uart, buf, sizeof(buf));
    /* n = 实际读取的字节数，< 0 表示错误 */

    /* === 5. 注册中断回调（可选） === */
    posix_uart_rx_cb_t cb =
    {
        .callback = on_uart_rx,
        .arg      = NULL,
    };
    posix_ioctl(uart, POSIX_UART_IOCTL_SET_RX_CB, &cb);
    /* 之后每收到一个字节，on_uart_rx 在 ISR 中被调用 */

    /* === 6. 等待发送完成（DMA 场景） === */
    posix_ioctl(uart, POSIX_UART_IOCTL_TX_FLUSH, NULL);

    /* === 7. 关闭 === */
    posix_close(uart);
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include "posix_port.h"
static bool s_uart_inited = false;

static int cmd_uart_test(const struct shell *sh, size_t argc, char **argv)
{
    if (!s_uart_inited) { posix_port_init_all(); s_uart_inited = true; }
    posix_fd_t fd = posix_open("/dev/uart0");
    if (fd == POSIX_FD_NULL) { shell_error(sh, "open /dev/uart0 failed"); return -1; }
    int n = posix_write(fd, "Hello POSIX\r\n", 14);
    shell_print(sh, "wrote %d bytes", n);
    posix_close(fd);
    shell_print(sh, "POSIX UART test PASSED");
    return 0;
}
SHELL_CMD_REGISTER(posix_uart, NULL, "POSIX UART smoke test", cmd_uart_test);
#endif /* CONFIG_SHELL */
