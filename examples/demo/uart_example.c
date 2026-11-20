/* ================================================================
 * UART usage example
 *
 * Function: open UART0 → configure 115200-8N1 → send data → receive data
 *       → register RX interrupt callback → close
 *
 * Build requirements: posix.h + posix_ioctl_uart.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_uart.h"
#include <string.h>

/* RX interrupt callback function */
static void on_uart_rx(posix_fd_t fd, uint8_t byte, void *arg)
{
    /* ISR context! No blocking calls allowed */
    /* Usually put the data into a ring buffer */
    (void)fd;
    (void)arg;
}

void example_uart(void)
{
    /* === 1. Open the device === */
    posix_fd_t uart = posix_open("/dev/uart0");
    if (!uart)
    {
        /* device does not exist or registration failed */
        return;
    }

    /* === 2. Configure parameters === */
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

    /* === 3. Send (blocking) === */
    const char *msg = "Hello UART!\r\n";
    posix_write(uart, msg, strlen(msg));

    /* === 4. Receive (non-blocking poll: returns 0 immediately when FIFO is empty) === */
    uint8_t buf[64];
    int n = posix_read(uart, buf, sizeof(buf));
    /* n = actual bytes read, < 0 means error */

    /* === 5. Register interrupt callback (optional) === */
    posix_uart_rx_cb_t cb =
    {
        .callback = on_uart_rx,
        .arg      = NULL,
    };
    posix_ioctl(uart, POSIX_UART_IOCTL_SET_RX_CB, &cb);
    /* after this, on_uart_rx is called from the ISR for every byte received */

    /* === 6. Wait for transmit to complete (DMA case) === */
    posix_ioctl(uart, POSIX_UART_IOCTL_TX_FLUSH, NULL);

    /* === 7. Close === */
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
