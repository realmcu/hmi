// Zephyr Shell test: uart:~$ posix_i2c [scan|probe <addr>]
/* ================================================================
 * I2C 总线使用示例
 *
 * 路径：/dev/i2c0、/dev/i2c1
 *
 * I2C 没有"数据流"语义，因此 posix_read / posix_write 直接返回 NOSUPP。
 * 所有事务通过 ioctl 表达，四个常用命令：
 *   POSIX_I2C_IOCTL_WRITE_REG  写子地址 + payload（单一 START/STOP）
 *   POSIX_I2C_IOCTL_READ_REG   写子地址 + repeated-start 读多字节
 *   POSIX_I2C_IOCTL_RAW_WRITE  纯写（不带子地址）
 *   POSIX_I2C_IOCTL_RAW_READ   纯读（不带子地址）
 *
 * 编译要求：需要 posix.h + posix_ioctl_i2c.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_i2c.h"

/* ---------------- 场景 1：配置总线并 ACK 探测某个从地址 ---------------- */
static int i2c_probe_addr(posix_fd_t bus, uint16_t addr)
{
    /* 0 字节写：多数从设备只对地址阶段回 ACK，可用来探活 */
    posix_i2c_msg_t m = { .addr = addr, .buf = NULL, .len = 0 };
    return posix_ioctl(bus, POSIX_I2C_IOCTL_RAW_WRITE, &m);
}

/* ---------------- 场景 2：读单个寄存器（1 字节子地址） ---------------- */
static int i2c_read_reg8(posix_fd_t bus, uint16_t addr, uint8_t reg, uint8_t *val)
{
    posix_i2c_msg_t m =
    {
        .addr    = addr,
        .reg     = reg,
        .reg_len = 1,
        .buf     = val,
        .len     = 1,
    };
    return posix_ioctl(bus, POSIX_I2C_IOCTL_READ_REG, &m);
}

/* ---------------- 场景 3：写单个寄存器 ---------------- */
static int i2c_write_reg8(posix_fd_t bus, uint16_t addr, uint8_t reg, uint8_t val)
{
    posix_i2c_msg_t m =
    {
        .addr    = addr,
        .reg     = reg,
        .reg_len = 1,
        .buf     = &val,
        .len     = 1,
    };
    return posix_ioctl(bus, POSIX_I2C_IOCTL_WRITE_REG, &m);
}

/* ---------------- 场景 4：burst 读（例：从 gsensor 读 6 字节三轴） ---------------- */
static int i2c_read_burst(posix_fd_t bus, uint16_t addr,
                          uint8_t reg, uint8_t *buf, size_t len)
{
    posix_i2c_msg_t m =
    {
        .addr    = addr,
        .reg     = reg,
        .reg_len = 1,
        .buf     = buf,
        .len     = len,
    };
    return posix_ioctl(bus, POSIX_I2C_IOCTL_READ_REG, &m);
}

/* ================================================================
 * 完整流程示例：打开 /dev/i2c0 → 配 400kHz → 探 SC7A20 → 读 WHO_AM_I → 读三轴
 * ================================================================ */
void example_i2c(void)
{
    /* === 1. 打开总线 === */
    posix_fd_t bus = posix_open("/dev/i2c0");
    if (!bus) { return; }

    /* === 2. 设置总线频率 400kHz（Fast mode） === */
    posix_i2c_config_t cfg =
    {
        .speed_hz  = POSIX_I2C_SPEED_FAST,
        .addr_bits = POSIX_I2C_ADDR_7BIT,
    };
    posix_ioctl(bus, POSIX_I2C_IOCTL_SET_CONFIG, &cfg);

    /* === 3. 探测 SC7A20 从地址（0x19 或 0x18） === */
    uint16_t sc7a20 = 0;
    if (i2c_probe_addr(bus, 0x19) == POSIX_OK) { sc7a20 = 0x19; }
    else if (i2c_probe_addr(bus, 0x18) == POSIX_OK) { sc7a20 = 0x18; }
    if (!sc7a20) { posix_close(bus); return; }

    /* === 4. 读 WHO_AM_I(0x0F)，SC7A20 应为 0x11 === */
    uint8_t who = 0;
    i2c_read_reg8(bus, sc7a20, 0x0F, &who);
    /* who == 0x11 */

    /* === 5. 写 CTRL_REG1 = 0x57 (ODR=100Hz, Normal, XYZ enable) === */
    i2c_write_reg8(bus, sc7a20, 0x20, 0x57);
    /* CTRL_REG4 = 0x80 (BDU, ±2g) */
    i2c_write_reg8(bus, sc7a20, 0x23, 0x80);

    /* === 6. burst 读 6 字节（X_L..Z_H），子地址 bit7=1 用于自增 === */
    uint8_t xyz[6] = {0};
    i2c_read_burst(bus, sc7a20, 0x28 | 0x80, xyz, 6);
    /* xyz[0..5] = x_l,x_h,y_l,y_h,z_l,z_h（左对齐 int16） */

    /* === 7. 关闭 === */
    posix_close(bus);
    (void)who;
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include "posix_port.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static bool s_i2c_inited = false;

/* 扫描 0x08~0x77（保留头/尾地址） */
static int do_i2c_scan(const struct shell *sh, const char *bus_path)
{
    posix_fd_t bus = posix_open(bus_path);
    if (bus == POSIX_FD_NULL) { shell_error(sh, "open %s failed", bus_path); return -1; }

    posix_i2c_config_t cfg =
    {
        .speed_hz  = POSIX_I2C_SPEED_STANDARD,   /* 扫描降到 100kHz 更稳 */
        .addr_bits = POSIX_I2C_ADDR_7BIT,
    };
    posix_ioctl(bus, POSIX_I2C_IOCTL_SET_CONFIG, &cfg);

    shell_print(sh, "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
    for (int row = 0; row < 8; row++)
    {
        char line[80]; int n = 0;
        n += snprintf(line + n, sizeof(line) - n, "%02x: ", row * 16);
        for (int col = 0; col < 16; col++)
        {
            int addr = row * 16 + col;
            if (addr < 0x08 || addr > 0x77)
            {
                n += snprintf(line + n, sizeof(line) - n, "   ");
                continue;
            }
            posix_i2c_msg_t m = { .addr = (uint16_t)addr, .buf = NULL, .len = 0 };
            int r = posix_ioctl(bus, POSIX_I2C_IOCTL_RAW_WRITE, &m);
            if (r == POSIX_OK)
            {
                n += snprintf(line + n, sizeof(line) - n, "%02x ", addr);
            }
            else
            {
                n += snprintf(line + n, sizeof(line) - n, "-- ");
            }
        }
        shell_print(sh, "%s", line);
    }

    posix_close(bus);
    return 0;
}

/* 单地址探测：写 0 字节看 ACK；如果成功再尝试读 WHO_AM_I */
static int do_i2c_probe(const struct shell *sh, const char *bus_path, uint16_t addr)
{
    posix_fd_t bus = posix_open(bus_path);
    if (bus == POSIX_FD_NULL) { shell_error(sh, "open %s failed", bus_path); return -1; }

    posix_i2c_config_t cfg =
    {
        .speed_hz  = POSIX_I2C_SPEED_STANDARD,
        .addr_bits = POSIX_I2C_ADDR_7BIT,
    };
    posix_ioctl(bus, POSIX_I2C_IOCTL_SET_CONFIG, &cfg);

    posix_i2c_msg_t probe = { .addr = addr, .buf = NULL, .len = 0 };
    int r = posix_ioctl(bus, POSIX_I2C_IOCTL_RAW_WRITE, &probe);
    if (r != POSIX_OK)
    {
        shell_error(sh, "no ACK at 0x%02x (ret=%d)", addr, r);
        posix_close(bus);
        return -1;
    }
    shell_print(sh, "ACK at 0x%02x", addr);

    /* 顺手读一个 WHO_AM_I 风格寄存器（0x0F 是很多传感器的 ID 位置） */
    uint8_t id = 0;
    posix_i2c_msg_t rd =
    {
        .addr = addr, .reg = 0x0F, .reg_len = 1, .buf = &id, .len = 1,
    };
    if (posix_ioctl(bus, POSIX_I2C_IOCTL_READ_REG, &rd) == POSIX_OK)
    {
        shell_print(sh, "reg[0x0F] = 0x%02x", id);
    }

    posix_close(bus);
    return 0;
}

static int cmd_i2c(const struct shell *sh, size_t argc, char **argv)
{
    if (!s_i2c_inited) { posix_port_init_all(); s_i2c_inited = true; }

    const char *bus_path = "/dev/i2c0";
    const char *sub = (argc >= 2) ? argv[1] : "scan";

    if (strcmp(sub, "scan") == 0)
    {
        return do_i2c_scan(sh, bus_path);
    }
    if (strcmp(sub, "probe") == 0 && argc >= 3)
    {
        uint16_t addr = (uint16_t)strtoul(argv[2], NULL, 0);
        return do_i2c_probe(sh, bus_path, addr);
    }
    shell_error(sh, "usage: posix_i2c scan | posix_i2c probe <addr>");
    return -1;
}

SHELL_CMD_REGISTER(posix_i2c, NULL,
                   "POSIX I2C test  (usage: posix_i2c scan | posix_i2c probe <addr>)",
                   cmd_i2c);
#endif /* CONFIG_SHELL */
