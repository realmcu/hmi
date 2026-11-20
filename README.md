# POSIX Device Abstraction Layer

Provides a **POSIX-style peripheral operation interface** for embedded RTOSes, decoupling the application layer from the underlying chip/OS.

```
#define POSIX_API_VERSION  "1.1"
#define POSIX_API_FUNCS    "open/close/read/write/ioctl"
#define POSIX_API_COVER    "UART/GPIO/SPI/PWM/ADC/SDIO/LCD/TOUCH/GSENSOR"
#define POSIX_API_EXTEND   "user-defined device (magic 0xF0~0xFF)"
```

## Design Goals

| Goal | Approach |
|------|------|
| Stable interface | Application code only calls the 5 functions `posix_open/read/write/ioctl/close` |
| Chip-independent | All hardware differences are encapsulated in the `port/` directory |
| RTOS-independent | Lock/ISR detection is overridden via weak symbols |
| Easy to extend | Adding a new device type = 1 .h + 1 .c + 1 registration line |
| Close to Linux style | The interface **looks like** Linux, but the semantics are not equivalent (see "Differences from Linux" below), so it cannot be migrated directly with sed |
| Zero dynamic allocation | The core uses static arrays, drivers use static pools |

## Architecture

```
┌──────────────────────────────────────────────────┐
│            Application code (app/*.c)            │
│ can only call posix_open/read/write/ioctl/close  │
├──────────────────────────────────────────────────┤
│         POSIX Core (core/posix_device.c)         │
│   device table → fd alloc → ops dispatch → ISR   │
├──────────────────────────────────────────────────┤
│     Auto initialization (core/posix_init.c)      │
│  linker section → driver init calls by priority  │
├──────────────────────────────────────────────────┤
│      Driver ops table (posix_driver_ops_t)       │
│   { .open .close .read .write .ioctl }            │
├──────────────────────────────────────────────────┤
│   port/ platform impl (the only part to edit)    │
│   UART|GPIO|SPI|PWM|ADC|SDIO|LCD|TOUCH|GSENSOR   │
├──────────────────────────────────────────────────┤
│        Chip hardware registers / RTOS API        │
└──────────────────────────────────────────────────┘
```

## File Structure

```
posix-io-abstraction/
├── README.md
├── PORTING.md                            ← Porting guide (must read)
├── include/
│   ├── posix.h                           ← Unified entry point
│   ├── posix_types.h                     ← Types / error codes
│   ├── posix_device.h                    ← Core interface + driver ops
│   ├── posix_ioctl.h                     ← ioctl encoding macros
│   ├── posix_init.h                      ← Auto-registration macro mechanism
│   └── ioctls/
│       ├── posix_ioctl_uart.h            ← UART config structs + cmd
│       ├── posix_ioctl_spi.h
│       ├── posix_ioctl_gpio.h
│       ├── posix_ioctl_pwm.h
│       ├── posix_ioctl_adc.h
│       ├── posix_ioctl_sdio.h
│       ├── posix_ioctl_lcd.h
│       ├── posix_ioctl_touch.h
│       └── posix_ioctl_gsensor.h
├── core/
│   ├── posix_device.c                    ← Core implementation (do not modify)
│   └── posix_init.c                      ← auto_init traversal implementation
└── examples/
    ├── demo/                             ← Application-layer usage examples
    │   ├── 01_uart_example.c             ← UART send/receive + interrupt callback
    │   ├── 02_gpio_example.c             ← GPIO pin-level fd
    │   ├── 03_spi_example.c              ← SPI full-duplex transfer
    │   ├── 04_pwm_example.c              ← PWM servo control
    │   ├── 05_adc_example.c              ← ADC single/continuous sampling
    │   ├── 06_sdio_example.c             ← SDIO block read/write
    │   ├── 07_lcd_example.c              ← LCD screen refresh
    │   ├── 08_touch_example.c            ← Touch point reading
    │   ├── 09_gsensor_example.c          ← G-sensor three-axis acceleration
    └── port/                             ← Platform port implementations
        ├── posix_init_zephyr.ld          ← Zephyr linker script reference
        ├── custom-rtos/                  ← Porting template (new RTOSes should follow this directory)
        │   ├── posix_port.h
        │   ├── posix_port_init.c         ← Lock + ISR detection
        │   ├── posix_port_uart.c
        │   ├── posix_port_gpio.c
        │   ├── posix_port_spi.c
        │   ├── posix_port_pwm.c
        │   ├── posix_port_adc.c
        │   ├── posix_port_sdio.c
        │   ├── posix_port_lcd.c
        │   ├── posix_port_touch.c
        │   └── posix_port_gsensor.c
        └── zephyr-rtk/                   ← RTK8773G + Zephyr implementation
            ├── posix_port.h
            ├── posix_port_init.c
            ├── posix_port_uart.c
            ├── posix_port_gpio.c
            ├── posix_port_spi.c
            ├── posix_port_lcd.c
            ├── posix_port_touch.c
            └── posix_port_gsensor.c
```

## Quick Start

### 1. Add it to your project

```makefile
# Makefile example
INCDIRS += posix-io-abstraction/include
SRCDIRS += posix-io-abstraction/core
SRCDIRS += posix-io-abstraction/port/custom-rtos    # replace with your platform directory
```

### 2. Port it to your RTOS (about 360 lines of C code)

```c
// port/your_rtos/posix_port_init.c

void posix_lock(void)   { /* your_rtos_mutex_lock(...) */ }
void posix_unlock(void) { /* your_rtos_mutex_unlock(...) */ }
int posix_port_in_isr(void) { /* return your_rtos_in_isr(); */ }
```

See [PORTING.md](PORTING.md) for detailed steps.

### 3. Initialize and write the application

```c
// main.c
#include "posix_port.h"
#include "ioctls/posix_ioctl_uart.h"
#include "ioctls/posix_ioctl_gpio.h"

void main(void)
{
    posix_port_init_all();

    posix_fd_t uart = posix_open("/dev/uart0");
    posix_write(uart, "Hello\r\n", 7);

    posix_fd_t led = posix_open("/dev/gpio0/p12");
    posix_gpio_config_t cfg = { .direction = 1 };
    posix_ioctl(led, POSIX_GPIO_IOCTL_SET_DIR, &cfg);
    posix_write(led, &(int){1}, sizeof(int));
}
```

## API Overview

| Function | Purpose | Supported by all devices |
|------|------|:---:|
| `posix_open(path)` | Open a device | ✓ |
| `posix_close(fd)` | Close a device | ✓ |
| `posix_read(fd, buf, len)` | Read data | UART/SPI/GPIO/ADC/SDIO |
| `posix_write(fd, buf, len)` | Write data | UART/SPI/GPIO/SDIO |
| `posix_ioctl(fd, cmd, arg)` | Control / configure | All (with different cmd) |
| `posix_read_isr(...)` | Read in ISR | Same as read, but without locking |
| `posix_write_isr(...)` | Write in ISR | Same as write, but without locking |
| `posix_ioctl_isr(...)` | Control in ISR | Same as ioctl, but without locking; determine the context with posix_port_in_isr() |
| `posix_auto_init()` | Auto-register all devices | Call once at startup |
| `posix_port_init_all()` | Initialize the lock + auto_init | Startup entry point |

## Steps to Add a New Device Type

Adding any new device type takes only 4 steps:

```
1. Define a magic in include/posix_device.h (0x0A~0xEF are available)
2. Write an ioctl header under include/ioctls/
3. Implement the 5 functions of posix_driver_ops_t
4. Call posix_device_register() to register + POSIX_INIT_DEVICE_EXPORT()
The core framework (include/*, core/*) and the port layer need no changes at all.
```

📝 Reference template: any driver file under `examples/port/custom-rtos/` (e.g. `posix_port_uart.c`)

## Thread Safety

| Level | Coverage | Notes |
|------|----------|------|
| ✅ **Framework layer** | `posix_open/close/read/write/ioctl` | Handle validation + table lookup + snapshot are done under the lock, and the driver ops are called **after the lock is released** (so ops can safely block without serializing other devices) |
| ⚠️ **Driver layer** | Access to hardware registers inside the driver | Guaranteed by the driver implementation itself |
| 🔒 **ISR variants** | `posix_*_isr()` | Skip the framework lock; only for interrupt context |

**The framework lock** is provided as a default empty implementation via a weak symbol (`void posix_lock/unlock(void) {}`),
when porting to a multi-threaded RTOS, just override it in `posix_port_init.c` with a mutex implementation:

```c
void posix_lock(void)   { rtos_mutex_lock(&g_mutex); }
void posix_unlock(void) { rtos_mutex_unlock(&g_mutex); }
```

> ⚠️ `posix_*_isr()` is used in ISRs and does not (and cannot) take a lock.
> If the ISR and task context share the same fd, the driver itself must protect the critical region with atomic operations or by disabling interrupts.

## Design Decisions

1. **Why use the `posix_` prefix?** To avoid symbol collisions with libc and to be self-documenting. Note that it is **not** an alias for the native Linux API: dropping the prefix is not equivalent either (see "Differences from Linux" below).

2. **Why does gpio use pin-level fds?** With the `/dev/gpio0/p12` style, each pin has its own fd, consistent with the Linux gpiolib. This suits scenarios with many pins.

3. **Why does the ioctl cmd encoding use magic+nr?** Similar to the Linux `_IOR/_IOW` scheme: the device type is encoded in the cmd value and dispatched by a switch inside the driver, so adding new types does not conflict.

4. **Why are the ISR-safe versions separate functions?** Because `posix_*_isr` skips the framework lock (mutexes are unavailable in an ISR) and calls the ops directly. **Use `posix_port_in_isr()` uniformly to determine the context**; the framework no longer stuffs a flag into the cmd—the old `posix_ioctl_isr` OR'ed in `POSIX_FLAG_ISR`, which polluted the cmd namespace and required every driver to mask it manually, so it has been removed.

5. **Why does the core use static arrays?** Embedded environments do not always have a heap. `POSIX_DEVICE_TABLE_SIZE` (default 32) and `POSIX_FD_POOL_SIZE` (default 16) can be configured before compilation.

6. **Why is auto-initialization (posix_auto_init) needed?** Driver registration uses the GCC `__attribute__((section))` attribute to place init function pointers into a dedicated ELF section, which is traversed and invoked at startup. This means adding a new driver requires no changes to any core code or port init function. The linker script must keep the `.posix$init*` section.

## Differences from Linux (no direct sed migration)

This layer is **POSIX-style**, not POSIX-compatible. The key differences from real Linux:

| Aspect | This layer | Linux user space |
|------|------|--------------|
| `open` | `posix_open(path)`, 1 argument, returns an opaque pointer, returns `POSIX_FD_NULL` on failure | `open(path, flags[, mode])`, returns an int fd, `-1`+`errno` on failure |
| `read/write` return | `posix_ssize_t`: `>=0` is a byte count, `<0` is a negative `POSIX_ERR_*` code | `ssize_t`: `-1` indicates an error, with the code in `errno` |
| Error model | Negative return values (kernel style), no global `errno` | `-1` + global `errno` |
| `ioctl` cmd | Custom `magic<<8 \| nr`, not `_IOC` encoding | `_IOC` encoding from `_IOR/_IOW/...` |

Therefore `sed 's/posix_//g'` **cannot** migrate directly: it would lose the flags/mode of `open`, the return types would not match, and all the error handling logic would be wrong. Migrating to Linux requires an adaptation shim rather than a pure text substitution.

## License

MIT
