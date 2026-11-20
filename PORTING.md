# POSIX Device Abstraction Layer — Porting Guide

## Overview

To port this abstraction layer to a new RTOS/chip platform, you need to do 3 things:

```
1. Implement the lock and ISR detection (3 lines of code)
2. Add one POSIX_INIT_DEVICE_EXPORT(fn) line at the end of each driver file
3. Keep the .posix$init* section in the linker script
4. Call posix_port_init_all() during system initialization
```

After porting, **all application-layer code stays unchanged**.

---

## Porting Steps

### Step 1: Implement the lock and ISR detection

In `port/your_rtos/posix_port_init.c`:

```c
#include "posix_port.h"

/* --- 1. Multi-threaded lock --- */
/* If your RTOS is multi-threaded, implement a mutex */

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

/* --- 2. ISR context detection --- */
int posix_port_in_isr(void)
{
    /* Most RTOSes provide a ready-made interface */
    return your_rtos_in_isr();

    /* If not, you can set a global flag with interrupts disabled:
     *
     * volatile int g_posix_in_isr = 0;
     * // on ISR entry: g_posix_in_isr = 1;
     * // on ISR exit: g_posix_in_isr = 0;
     * return g_posix_in_isr;
     */
}
```

**Single-threaded / bare-metal scenario**: leave `posix_lock/unlock` empty.

> 🔒 **Scope of the lock**: the framework lock only protects framework metadata such as the device table / fd pool.
> Each API performs "handle validation + table lookup + ops/file_priv snapshot" while holding the lock, and only invokes the driver ops callback **after releasing the lock**.
> Therefore driver ops can safely block/sleep and will **not** serialize other devices or stall the whole system by holding a global lock;
> While an fd is open, the corresponding device's ref_count > 0, ensuring the device cannot be unregistered concurrently when ops run outside the lock.

### Step 2: Implement each peripheral driver

Each peripheral needs to implement 5 functions (open/close/read/write/ioctl) that form a `posix_driver_ops_t`.
(read/write return `posix_ssize_t`; a failing open must return `POSIX_OPEN_ERR`.)

Estimated minimum porting effort:

| Peripheral | open | close | read | write | ioctl | Total lines |
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

**Total: about 490 lines of C code.**

See `examples/port/custom-rtos/posix_port_*.c` for concrete implementation templates.

### Step 3: Driver registration (automatic)

Each driver registers itself automatically via `POSIX_INIT_DEVICE_EXPORT`; no manual call is needed in the port layer:

```c
// end of examples/port/custom-rtos/posix_port_uart.c
static int uart_init(void)
{
    return posix_device_register_group("/dev/uart%d", 2, &g_uart_ops, privs);
}
POSIX_INIT_DEVICE_EXPORT(uart_init);
```

At startup, `posix_auto_init()` walks the linker section and calls every driver registration function automatically.

### Step 4: Linker script

In the linker script, **list each section explicitly in priority order** (you cannot use the wildcard `*`, otherwise the priority order is not guaranteed):

```ld
.posix_init : {
    KEEP(*(.posix$initS))   /* start sentinel (provided by posix_init.c; do not define the symbol here) */
    KEEP(*(.posix$init0))   /* priority 0: board init (clock/pinmux) */
    KEEP(*(.posix$init1))   /* priority 1: device driver registration */
    KEEP(*(.posix$init2))   /* priority 2: application init */
    KEEP(*(.posix$initE))   /* end sentinel (provided by posix_init.c; do not define the symbol here) */
} > FLASH
```

> ⚠️ **Do not** define the `__posix_init_start` / `__posix_init_end` symbols yourself in the linker script;
> they are provided by the C array sentinels in `posix_init.c`. Duplicate definitions will corrupt the iteration bounds.

---

## Porting Checklist

### UART
- [ ] `open`: allocate the file struct, set the default baud rate to 115200
- [ ] `read`: blocking read in task context, non-blocking read in ISR context
- [ ] `write`: blocking write in task context, byte-by-byte write to the TX FIFO in ISR context
- [ ] `ioctl SET_CONFIG`: set baud rate / data bits / parity / stop bits / flow control
- [ ] `ioctl SET_RX_CB`: register the RX interrupt callback
- [ ] Does DMA transfer require `cache_flush` / `cache_invalidate`?

### GPIO (pin-level fd)
- [ ] `open`: parse the path `/dev/gpio0/p12`, bind the GPIO controller and pin number
- [ ] `read`: read the pin level
- [ ] `write`: write the pin level
- [ ] `ioctl SET_DIR`: configure direction + pull-up/pull-down
- [ ] `ioctl SET_IRQ`: configure the interrupt trigger mode
- [ ] `ioctl ENABLE/DISABLE_IRQ`: enable/disable the interrupt
- [ ] Is it safe to call `posix_write_isr` from an interrupt callback?

### SPI
- [ ] `ioctl SET_CONFIG`: configure mode / frequency / word size
- [ ] `ioctl TRANSFER`: full-duplex transfer
- [ ] `ioctl CS_TAKE/RELEASE`: chip-select control
- [ ] DMA transfer alignment requirements

### PWM
- [ ] `ioctl SET_CONFIG`: frequency + duty cycle
- [ ] `ioctl START/STOP`: start/stop control

### ADC
- [ ] `ioctl SET_CONFIG`: resolution / sample rate
- [ ] `read`: single-channel sampling
- [ ] `ioctl READ_CHANNEL`: sample a specified channel
- [ ] `ioctl START_CONTINUOUS`: continuous mode (optional)

### SDIO
- [ ] `ioctl SET_CONFIG`: bus width / speed mode
- [ ] `ioctl READ/WRITE_BLOCKS`: block read/write (mind the alignment)
- [ ] `ioctl GET_INFO`: get card capacity information
- [ ] Is 4-bit/8-bit bus switching required?

### LCD
- [ ] `ioctl SET_CONFIG`: resolution / color depth / interface type
- [ ] `ioctl SET_WINDOW`: window clipping region
- [ ] `write`: flush pixel data to the screen
- [ ] `ioctl DISPLAY_ON/OFF`: display on/off
- [ ] `ioctl SET_BRIGHTNESS`: brightness adjustment

### Touch
- [ ] `ioctl SET_CONFIG`: I2C address / resolution
- [ ] `read`: read touch point coordinates
- [ ] `ioctl CALIBRATE`: touch calibration
- [ ] `ioctl SET_POWER`: sleep/wake control

### G-sensor
- [ ] `ioctl SET_CONFIG`: range / output data rate
- [ ] `read`: read three-axis acceleration
- [ ] `ioctl READ_TEMP`: chip temperature
- [ ] `ioctl SELF_TEST`: self-test

---

## FAQ

### Q: My RTOS does not support dynamic memory allocation. What should I do?
Each open needs to allocate a `file_priv` structure. You can pre-allocate a static pool:

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

### Q: I cannot take a lock inside an ISR. How can thread safety be guaranteed?
The framework layer provides lock protection for the device table/fd pool for `posix_open/close/read/write/ioctl`,
but **driver ops run outside the lock**; the ISR variants `posix_*_isr()` skip the framework lock entirely.

Inside driver ops, use `posix_port_in_isr()` to determine the context:
- Task context (when called via `posix_read/write/ioctl`): **the framework lock is already released before the ops are called**, so blocking and sleeping are allowed
- ISR context (when called via `posix_*_isr`): **no framework lock**, so only non-blocking access to the FIFO is allowed

> ⚠️ If the ISR and task context access hardware registers through the same driver,
> the driver itself must protect the critical region with **atomic operations** or by **disabling interrupts**. The framework lock is not available in an ISR.

### Q: My chip has only one UART. Do I still need to use a path?
Yes. `posix_device_register("/dev/uart0", ...)` is just a string and does not affect functionality. You can also simplify it to `posix_device_register("uart", ...)` and use any string you like.

### Q: How many steps does it take to add a new peripheral type (e.g. LCD)?

```
1. Write a header under include/ioctls/:
   define the struct and ioctl cmd needed by the peripheral

2. Implement the 5 functions of posix_driver_ops_t

3. Add init at the end + the auto-registration macro:
   static int lcd_init(void) {
       return posix_device_register("/dev/lcd0", &lcd_ops, &lcd_priv);
   }
   POSIX_INIT_DEVICE_EXPORT(lcd_init);
```

📝 Reference template: any driver file under `examples/port/custom-rtos/` (e.g. `posix_port_uart.c`)
No line of the core framework changes, no line of the port init changes, and the linker collects it automatically.

### Q: What if I later switch to real Linux?
You **cannot** simply run `sed 's/posix_//g'`. This layer is POSIX-*style*: the interfaces look similar, but the semantics are not equivalent:
- `posix_open(path)` takes only 1 argument, returns an opaque pointer, and returns `POSIX_FD_NULL` on failure;
  whereas Linux `open(path, flags[, mode])` returns an int fd and `-1`+`errno` on failure.
- `posix_read/write` return `posix_ssize_t` (`>=0` byte count / `<0` negative error code);
  Linux returns `ssize_t`, where an error is `-1` and the error code is in the global `errno`.
- The cmd of `posix_ioctl` is `magic<<8 | nr`, not the Linux `_IOC` encoding.

Migrating to Linux requires writing an adaptation shim (wrapping open flags, converting negative return values to `-1`+`errno`, etc.),
not a pure text substitution. The value of the prefix lies in avoiding symbol collisions and being self-documenting, not in "being sed-able into a Linux API".
