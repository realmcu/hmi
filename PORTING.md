# POSIX 设备抽象层 — 移植指南

## 概述

将本抽象层移植到新的 RTOS/芯片平台，你需要做 3 件事情：

```
1. 实现锁和 ISR 检测（3 行代码）
2. 在每个驱动文件尾部加一行 POSIX_INIT_DEVICE_EXPORT(fn)
3. 在链接脚本中保留 .posix$init* 段
4. 在系统初始化时调用 posix_port_init_all()
```

移植完成后，**所有应用层代码零修改**。

---

## 移植步骤

### 第一步：实现锁和 ISR 检测

在 `port/你的RTOS/posix_port_init.c` 中：

```c
#include "posix_port.h"

/* --- 1. 多线程锁 --- */
/* 如果你的 RTOS 是多线程的，实现互斥锁 */

static YOUR_MUTEX_T s_posix_mutex;

void posix_lock(void)
{
    your_rtos_mutex_lock(&s_posix_mutex);
}

void posix_unlock(void)
{
    your_rtos_mutex_unlock(&s_posix_mutex);
}

void posix_port_lock_init(void)
{
    your_rtos_mutex_create(&s_posix_mutex);
}

/* --- 2. ISR 上下文检测 --- */
int posix_port_in_isr(void)
{
    /* 大部分 RTOS 有现成的接口 */
    return your_rtos_in_isr();

    /* 如果没有，可以关中断时设一个全局标志：
     *
     * volatile int g_posix_in_isr = 0;
     * // 进入 ISR 时：g_posix_in_isr = 1;
     * // 退出 ISR 时：g_posix_in_isr = 0;
     * return g_posix_in_isr;
     */
}
```

**单线程/裸机场景**：`posix_lock/unlock` 留空即可。

> 🔒 **锁的作用范围**：框架锁只保护设备表 / fd 池等框架元数据。
> 每个 API 在锁内完成“句柄校验 + 查表 + 取 ops/file_priv 快照”，**释放锁后**再执行驱动 ops 回调。
> 因此驱动 ops 可以安全阻塞/睡眠，**不会**因持有全局锁而串行化其它设备或导致全系统假死；
> 同一 fd 打开期间对应设备 ref_count > 0，保证锁外执行 ops 时设备不会被并发注销。

### 第二步：实现各外设驱动程序

每个外设需要实现 5 个函数（open/close/read/write/ioctl），构成一个 `posix_driver_ops_t`。
（read/write 返回 `posix_ssize_t`；open 失败须返回 `POSIX_OPEN_ERR`。）

最小移植工作量估算：

| 外设 | open | close | read | write | ioctl | 总行数 |
|------|------|-------|------|-------|-------|--------|
| UART | 5 | 3 | 15 | 15 | 40 | ~80 |
| GPIO | 15 | 3 | 5 | 5 | 50 | ~80 |
| SPI | 5 | 3 | 5 | 5 | 40 | ~60 |
| PWM | 5 | 3 | — | — | 30 | ~40 |
| ADC | 5 | 3 | 10 | — | 20 | ~40 |
| SDIO | 5 | 3 | 10 | 10 | 30 | ~60 |
| LCD | 5 | 3 | — | 10 | 30 | ~50 |
| Touch | 5 | 3 | 10 | — | 20 | ~40 |
| GSensor | 5 | 3 | 10 | — | 20 | ~40 |

**总计：约 490 行 C 代码。**

具体实现模板见 `port/custom-rtos/posix_port_*.c`。

### 第三步：驱动注册（自动）

每个驱动通过 `POSIX_INIT_DEVICE_EXPORT` 自动注册，无需在 port 层手动调用：

```c
// port/custom-rtos/posix_port_uart.c 末尾
static int uart_init(void)
{
    return posix_device_register_group("/dev/uart%d", 2, &g_uart_ops, privs);
}
POSIX_INIT_DEVICE_EXPORT(uart_init);
```

启动时 `posix_auto_init()` 遍历链接段，自动调用所有驱动注册函数。

### 第四步：链接脚本

在链接脚本中保留 `.posix$init*` 段：

```ld
.posix_init : {
    __posix_init_start = .;
    KEEP(*(.posix$init*))
    __posix_init_end = .;
} > FLASH
```

---

## 移植检查清单

### UART
- [ ] `open`: 分配 file 结构体，设默认波特率 115200
- [ ] `read`: 任务上下文阻塞读，ISR 上下文非阻塞读
- [ ] `write`: 任务上下文阻塞写，ISR 上下文逐字节写 TX FIFO
- [ ] `ioctl SET_CONFIG`: 设置波特率/数据位/校验/停止位/流控
- [ ] `ioctl SET_RX_CB`: 注册接收中断回调
- [ ] DMA 传输是否需要 `cache_flush` / `cache_invalidate`？

### GPIO（引脚级 fd）
- [ ] `open`: 解析路径 `/dev/gpio0/p12`，绑定 GPIO 控制器和 pin 号
- [ ] `read`: 读引脚电平
- [ ] `write`: 写引脚电平
- [ ] `ioctl SET_DIR`: 配置方向 + 上下拉
- [ ] `ioctl SET_IRQ`: 配置中断触发方式
- [ ] `ioctl ENABLE/DISABLE_IRQ`: 中断使能/禁能
- [ ] 中断回调中调用 `posix_write_isr` 是否安全？

### SPI
- [ ] `ioctl SET_CONFIG`: 配置模式/频率/字长
- [ ] `ioctl TRANSFER`: 全双工传输
- [ ] `ioctl CS_TAKE/RELEASE`: 片选控制
- [ ] DMA 传输 alignment 要求

### PWM
- [ ] `ioctl SET_CONFIG`: 频率 + 占空比
- [ ] `ioctl START/STOP`: 启停控制

### ADC
- [ ] `ioctl SET_CONFIG`: 分辨率/采样率
- [ ] `read`: 单通道采样
- [ ] `ioctl READ_CHANNEL`: 指定通道采样
- [ ] `ioctl START_CONTINUOUS`: 连续模式（可选）

### SDIO
- [ ] `ioctl SET_CONFIG`: 总线宽度/速度模式
- [ ] `ioctl READ/WRITE_BLOCKS`: 块读写（注意对齐）
- [ ] `ioctl GET_INFO`: 获取卡容量信息
- [ ] 是否需要 4bit/8bit 总线切换？

### LCD
- [ ] `ioctl SET_CONFIG`: 分辨率/色深/接口类型
- [ ] `ioctl SET_WINDOW`: 窗口裁剪区域
- [ ] `write`: 像素数据刷屏
- [ ] `ioctl DISPLAY_ON/OFF`: 显示开关
- [ ] `ioctl SET_BRIGHTNESS`: 亮度调节

### Touch
- [ ] `ioctl SET_CONFIG`: I2C 地址/分辨率
- [ ] `read`: 读取触摸点坐标
- [ ] `ioctl CALIBRATE`: 触摸校准
- [ ] `ioctl SET_POWER`: 休眠/唤醒控制

### G-sensor
- [ ] `ioctl SET_CONFIG`: 量程/输出数据率
- [ ] `read`: 读取三轴加速度
- [ ] `ioctl READ_TEMP`: 芯片温度
- [ ] `ioctl SELF_TEST`: 自检功能

---

## 常见问题

### Q: 我的 RTOS 不支持动态内存分配，怎么办？
每个 open 需要分配一个 `file_priv` 结构。可以预分配静态池：

```c
#define MAX_UART_FILES  4
static uart_file_t s_uart_files[MAX_UART_FILES];
static int s_uart_file_used[MAX_UART_FILES];

static void *uart_open(void *drv_data, const char *path)
{
    for (int i = 0; i < MAX_UART_FILES; i++) {
        if (!s_uart_file_used[i]) {
            s_uart_file_used[i] = 1;
            return &s_uart_files[i];
        }
    }
    return NULL;
}
```

### Q: ISR 中不能加锁，怎么保证线程安全？
框架层为 `posix_open/close/read/write/ioctl` 提供了设备表/fd 池的加锁保护，
但**驱动 ops 在锁外执行**；ISR 版本 `posix_*_isr()` 则完全跳过框架锁。

驱动 ops 中通过 `posix_port_in_isr()` 判断上下文：
- 任务上下文（通过 `posix_read/write/ioctl` 调用时）：**框架锁已在调 ops 前释放**，可阻塞、可睡眠
- ISR 上下文（通过 `posix_*_isr` 调用时）：**无框架锁**，非阻塞、只操作 FIFO

> ⚠️ 如果 ISR 和任务上下文通过同一驱动访问硬件寄存器，
> 驱动自身需要用**原子操作**或**关中断**保护关键区。框架锁在 ISR 中不可用。

### Q: 我的芯片只有一个 UART，也要用路径吗？
可以。`posix_device_register("/dev/uart0", ...)` 只是一个字符串，不影响功能。你也可以简化为 `posix_device_register("uart", ...)` 用任意字符串。

### Q: 添加一种新的外设类型（如 LCD）需要几步？

```
1. 在 include/ioctls/ 下写头文件:
   定义外设所需的 struct 和 ioctl cmd

2. 实现 posix_driver_ops_t 的 5 个函数

3. 尾部加 init + 自动注册宏:
   static int lcd_init(void) {
       return posix_device_register("/dev/lcd0", &lcd_ops, &lcd_priv);
   }
   POSIX_INIT_DEVICE_EXPORT(lcd_init);
```

📝 参考模板: `examples/10_new_device_template.c`
核心框架一行不改，port 初始化一行不改，链接器自动收集。

### Q: 未来换到真正的 Linux 怎么办？
**不能**简单 `sed 's/posix_//g'`。本层是 POSIX *风格*，接口形似但语义并不等价：
- `posix_open(path)` 只有 1 个参数、返回不透明指针、失败返回 `POSIX_FD_NULL`；
  而 Linux `open(path, flags[, mode])` 返回 int fd、失败 `-1`+`errno`。
- `posix_read/write` 返回 `posix_ssize_t`（`>=0` 字节数 / `<0` 负错误码）；
  Linux 返回 `ssize_t`，错误是 `-1` 且错误码在全局 `errno`。
- `posix_ioctl` 的 cmd 是 `magic<<8 | nr`，不是 Linux 的 `_IOC` 编码。

迁移到 Linux 需要写一层适配 shim（封装 open flags、把负返回值转成 `-1`+`errno` 等），
而非纯文本替换。前缀的价值在于避免符号冲突与自文档化，不在于“可 sed 成 Linux API”。
