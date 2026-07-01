# 使用示例索引

| 文件 | 外设 | 演示内容 |
|------|------|----------|
| [01_uart_example.c](01_uart_example.c) | UART | 配置 → 收发 → 中断回调 |
| [02_gpio_example.c](02_gpio_example.c) | GPIO | 引脚级 fd → LED/按键 → 中断 |
| [03_spi_example.c](03_spi_example.c) | SPI | 全双工 → Flash 读写 → CS 控制 |
| [04_pwm_example.c](04_pwm_example.c) | PWM | 舵机 → 频率/脉宽调节 |
| [05_adc_example.c](05_adc_example.c) | ADC | 单次采样 → 连续采样 |
| [06_sdio_example.c](06_sdio_example.c) | SDIO | 卡信息 → 块读写 → MBR |
| [07_lcd_example.c](07_lcd_example.c) | LCD | 配置 → 窗口 → 刷屏 |
| [08_touch_example.c](08_touch_example.c) | Touch | 配置 → 校准 → 读触摸点 |
| [09_gsensor_example.c](09_gsensor_example.c) | G-sensor | 配置量程 → 读三轴 → 温度 |
| [10_i2c_example.c](10_i2c_example.c) | I²C | 总线扫描 → 探测 → 寄存器读写 → burst |

所有 example 中 `posix_` API 跨平台通用，换芯片/换 OS 时**零修改**。
