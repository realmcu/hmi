# Usage example index

| File | Peripheral | Demo content |
|------|------------|--------------|
| [01_uart_example.c](01_uart_example.c) | UART | configure → send/receive → interrupt callback |
| [02_gpio_example.c](02_gpio_example.c) | GPIO | pin-level fd → LED/button → interrupt |
| [03_spi_example.c](03_spi_example.c) | SPI | full duplex → Flash read/write → CS control |
| [04_pwm_example.c](04_pwm_example.c) | PWM | servo → frequency/pulse width adjustment |
| [05_adc_example.c](05_adc_example.c) | ADC | single sample → continuous sampling |
| [06_sdio_example.c](06_sdio_example.c) | SDIO | card info → block read/write → MBR |
| [07_lcd_example.c](07_lcd_example.c) | LCD | configure → window → screen flush |
| [touch_example.c](touch_example.c) | Touch (CST816D / CHSC6417) | /dev/touch0 unified entry → read touch points → runtime bind alias |
| [09_gsensor_example.c](09_gsensor_example.c) | G-sensor | configure range → read three axes → temperature |
| [10_i2c_example.c](10_i2c_example.c) | I²C | bus scan → probe → register read/write → burst |

The `posix_` API in all examples is cross-platform, with **zero changes** when switching chip or OS.
