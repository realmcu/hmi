// Zephyr Shell test: uart:~$ posix_i2c [scan|probe <addr>]
/* ================================================================
 * I2C 总线使用示例
 *
 * 路径：/dev/i2c1（i2c0 在当前板级 overlay 中未 enabled）
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
/* RTL87x3G I2C 驱动不支持 0 字节写，改用读 1 字节探测 ACK */
static int i2c_probe_addr(posix_fd_t bus, uint16_t addr)
{
    uint8_t dummy = 0;
    posix_i2c_msg_t m = { .addr = addr, .buf = &dummy, .len = 1 };
    return posix_ioctl(bus, POSIX_I2C_IOCTL_RAW_READ, &m);
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
 * 完整流程示例：打开 /dev/i2c1 → 配 400kHz → 探 SC7A20 → 读 WHO_AM_I → 读三轴
 * ================================================================ */
void example_i2c(void)
{
    /* === 1. 打开总线 === */
    posix_fd_t bus = posix_open("/dev/i2c1");
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
            /* RTL87x3G I2C 驱动不支持 0 字节写（返回 -EINVAL），
             * 改用读 1 字节探测 ACK，有设备响应则返回 POSIX_OK */
            uint8_t dummy = 0;
            posix_i2c_msg_t m = { .addr = (uint16_t)addr, .buf = &dummy, .len = 1 };
            int r = posix_ioctl(bus, POSIX_I2C_IOCTL_RAW_READ, &m);
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

/* 单地址探测：
 * 普通设备：RAW_READ 1 字节看 ACK
 * CHSC6417（0x2E）：需先写 4 字节地址 0x2c000020 才会响应，特殊处理 */
static int do_i2c_probe(const struct shell *sh, const char *bus_path, uint16_t addr)
{
    posix_fd_t bus = posix_open(bus_path);
    if (bus == POSIX_FD_NULL) { shell_error(sh, "open %s failed", bus_path); return -1; }

    posix_i2c_config_t cfg =
    {
        .speed_hz  = POSIX_I2C_SPEED_FAST,
        .addr_bits = POSIX_I2C_ADDR_7BIT,
    };
    posix_ioctl(bus, POSIX_I2C_IOCTL_SET_CONFIG, &cfg);

    int r;
    if (addr == 0x2E)
    {
        /* CHSC6417 特殊协议：写 4 字节地址再读 1 字节 */
        uint32_t write_addr = 0x2c000020u;
        posix_i2c_msg_t wr = { .addr = addr, .reg = 0, .reg_len = 0,
                               .buf = (uint8_t *) &write_addr, .len = 4
                             };
        r = posix_ioctl(bus, POSIX_I2C_IOCTL_RAW_WRITE, &wr);
        if (r == POSIX_OK)
        {
            uint8_t dummy = 0;
            posix_i2c_msg_t rd = { .addr = addr, .reg = 0, .reg_len = 0,
                                   .buf = &dummy, .len = 1
                                 };
            r = posix_ioctl(bus, POSIX_I2C_IOCTL_RAW_READ, &rd);
        }
    }
    else
    {
        uint8_t dummy = 0;
        posix_i2c_msg_t probe = { .addr = addr, .buf = &dummy, .len = 1 };
        r = posix_ioctl(bus, POSIX_I2C_IOCTL_RAW_READ, &probe);
    }

    if (r != POSIX_OK)
    {
        shell_error(sh, "no ACK at 0x%02x (ret=%d)", addr, r);
        posix_close(bus);
        return -1;
    }
    shell_print(sh, "ACK at 0x%02x", addr);

    posix_close(bus);
    return 0;
}

/* CHSC6417 读取：先写 4 字节地址 0x2c000020，再读 8 字节
 * 与 Zephyr chsc6417 驱动 chsc6x_process() 协议一致 */
static int do_touch_read(const struct shell *sh, const char *bus_path)
{
    posix_fd_t bus = posix_open(bus_path);
    if (bus == POSIX_FD_NULL) { shell_error(sh, "open %s failed", bus_path); return -1; }

    posix_i2c_config_t cfg = { .speed_hz = POSIX_I2C_SPEED_FAST, .addr_bits = POSIX_I2C_ADDR_7BIT };
    posix_ioctl(bus, POSIX_I2C_IOCTL_SET_CONFIG, &cfg);

    /* 写 4 字节地址 */
    uint32_t write_addr = 0x2c000020u;
    posix_i2c_msg_t wr = { .addr = 0x2E, .reg = 0, .reg_len = 0, .buf = (uint8_t *) &write_addr, .len = 4 };
    int r = posix_ioctl(bus, POSIX_I2C_IOCTL_RAW_WRITE, &wr);
    if (r != POSIX_OK) { shell_error(sh, "write addr failed: %d", r); posix_close(bus); return -1; }

    /* 读 8 字节 */
    uint8_t output[8] = {0};
    posix_i2c_msg_t rd = { .addr = 0x2E, .reg = 0, .reg_len = 0, .buf = output, .len = 8 };
    r = posix_ioctl(bus, POSIX_I2C_IOCTL_RAW_READ, &rd);
    if (r != POSIX_OK) { shell_error(sh, "read failed: %d", r); posix_close(bus); return -1; }

    shell_print(sh, "raw[8]: %02x %02x %02x %02x %02x %02x %02x %02x",
                output[0], output[1], output[2], output[3],
                output[4], output[5], output[6], output[7]);

    /* 有效帧的 header 应为 0xff；否则视为无数据/仲裁失败/竞争污染 */
    if (output[0] != 0xff)
    {
        shell_print(sh, "invalid frame (header=0x%02x, expect 0xff)", output[0]);
        posix_close(bus);
        return 0;
    }

    uint8_t point_num = output[1];
    shell_print(sh, "point_num = %d", point_num);
    if (point_num > 0)
    {
        uint8_t x_l8 = output[2], y_l8 = output[3];
        uint8_t x_h4 = output[5] & 0x0F, y_h4 = (output[5] >> 4) & 0x0F;
        uint8_t event = output[6] & 0x0F;
        uint16_t x = ((uint16_t)x_h4 << 8) | x_l8;
        uint16_t y = ((uint16_t)y_h4 << 8) | y_l8;
        shell_print(sh, "x=%d y=%d event=%d (%s)", x, y, event,
                    event == 0 ? "press" : "release");
    }

    posix_close(bus);
    return 0;
}

static int cmd_i2c(const struct shell *sh, size_t argc, char **argv)
{
    if (!s_i2c_inited) { posix_port_init_all(); s_i2c_inited = true; }

    /* argv[1] = 子命令; argv[2..] = 参数
     * 允许通过 "bus=i2c0" / "bus=i2c1" 选总线，缺省 i2c1（touch）
     * 例：
     *   posix_i2c scan bus=i2c0
     *   posix_i2c probe 0x19 bus=i2c0
     */
    const char *sub = (argc >= 2) ? argv[1] : "scan";
    const char *bus_path = "/dev/i2c1";
    /* 从末尾扫描 bus= 前缀，覆盖默认值 */
    for (size_t i = 1; i < argc; i++)
    {
        if (strncmp(argv[i], "bus=i2c0", 8) == 0) { bus_path = "/dev/i2c0"; }
        else if (strncmp(argv[i], "bus=i2c1", 8) == 0) { bus_path = "/dev/i2c1"; }
    }
    shell_print(sh, "using %s", bus_path);

    if (strcmp(sub, "scan") == 0)
    {
        return do_i2c_scan(sh, bus_path);
    }
    if (strcmp(sub, "probe") == 0 && argc >= 3)
    {
        uint16_t addr = (uint16_t)strtoul(argv[2], NULL, 0);
        return do_i2c_probe(sh, bus_path, addr);
    }
    if (strcmp(sub, "touch") == 0)
    {
        return do_touch_read(sh, bus_path);
    }
    shell_error(sh, "usage: posix_i2c scan [bus=i2c0|bus=i2c1]"
                " | posix_i2c probe <addr> [bus=i2c0|bus=i2c1]"
                " | posix_i2c touch");
    return -1;
}

SHELL_CMD_REGISTER(posix_i2c, NULL,
                   "POSIX I2C test  (usage: posix_i2c scan | posix_i2c probe <addr> | posix_i2c touch)",
                   cmd_i2c);
#endif /* CONFIG_SHELL */
