/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <string.h>
#include "mem_config.h"
#include "trace.h"
#include "cmsis_compiler.h"



typedef void (*IRQ_Fun)(void);       /**< ISR Handler Prototype */


bool Reset_Handler(void) __attribute__((section(".image_entry")));

void NMI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void System_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void WDT_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void BTMAC_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void DSP_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void RXI300_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void SPI0_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void I2C0_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void ADC_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void SPORT0_TX_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void SPORT0_RX_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void TIM2_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void TIM3_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void TIM4_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void RTC_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void UART0_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void UART1_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void UART2_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void Peri_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void GPIO_A0_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIO_A1_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIO_A_2_7_Handler(void) __attribute__((weak, alias("Default_Handler")));
void GPIO_A_8_15_Handler(void)__attribute__((weak, alias("Default_Handler")));
void GPIO_A_16_23_Handler(void)__attribute__((weak, alias("Default_Handler")));
void GPIO_A_24_31_Handler(void)__attribute__((weak, alias("Default_Handler")));
void SPORT1_RX_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void SPORT1_TX_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void ADP_DET_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void VBAT_DET_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void GDMA0_Channel0_Handler(void) __attribute__((weak, alias("Default_Handler")));
void GDMA0_Channel1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void GDMA0_Channel2_Handler(void) __attribute__((weak, alias("Default_Handler")));
void GDMA0_Channel3_Handler(void) __attribute__((weak, alias("Default_Handler")));
void GDMA0_Channel4_Handler(void) __attribute__((weak, alias("Default_Handler")));
void GDMA0_Channel5_Handler(void) __attribute__((weak, alias("Default_Handler")));
void GDMA0_Channel6_Handler(void) __attribute__((weak, alias("Default_Handler")));
void GDMA0_Channel7_Handler(void) __attribute__((weak, alias("Default_Handler")));
void GDMA0_Channel8_Handler(void) __attribute__((weak, alias("Default_Handler")));
void GPIO_B_0_7_Handler(void) __attribute__((weak, alias("Default_Handler")));
void GPIO_B_8_15_Handler(void)__attribute__((weak, alias("Default_Handler")));
void GPIO_B_16_23_Handler(void)__attribute__((weak, alias("Default_Handler")));
void GPIO_B_24_31_Handler(void)__attribute__((weak, alias("Default_Handler")));
void SPI1_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void SPI2_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void I2C1_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void I2C2_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void KeyScan_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void QDEC_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void USB_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void USB_ISOC_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void SPIC0_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void SPIC1_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void SPIC2_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void SPIC3_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void TIM5_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void TIM6_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void TIM7_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void ASRC0_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void ASRC1_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void I8080_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void ISO7816_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void SDIO0_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void SPORT2_RX_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void ANC_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void TOUCH_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void GDMA0_Channel9_Handler(void) __attribute__((weak, alias("Default_Handler")));
void GDMA0_Channel10_Handler(void) __attribute__((weak, alias("Default_Handler")));
void GDMA0_Channel11_Handler(void) __attribute__((weak, alias("Default_Handler")));
void Display_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void PPE_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void IMDC_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void Slave_Port_Monitor_Handler(void) __attribute__((weak, alias("Default_Handler")));
void RTK_Timer0_Handler(void) __attribute__((weak, alias("Default_Handler")));
void RTK_Timer1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void RTK_Timer2_Handler(void) __attribute__((weak, alias("Default_Handler")));
void RTK_Timer3_Handler(void) __attribute__((weak, alias("Default_Handler")));
void CAN_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void BTMAC_WRAP_AROUND_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SHA256_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void Public_Key_Engine_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SPI_PHY1_INTR_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MFB_DET_L_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void PTA_Mailbox_Handler(void) __attribute__((weak, alias("Default_Handler")));
void Utmi_Suspend_N_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IR_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void TRNG_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void PSRAMC_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void LPCOMP_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void ADP_IN_DET_Handler(void) __attribute__((weak, alias("Default_Handler")));
void ADP_OUT_DET_Handler(void) __attribute__((weak, alias("Default_Handler")));
void GPIOA2_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA3_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA4_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA5_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA6_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA7_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA8_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA9_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA10_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA11_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA12_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA13_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA14_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA15_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA16_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA17_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA18_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA19_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA20_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA21_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA22_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA23_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA24_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA25_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA26_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA27_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA28_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA29_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA30_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOA31_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB0_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB1_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB2_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB3_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB4_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB5_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB6_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB7_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB8_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB9_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB10_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB11_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB12_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB13_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB14_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB15_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB16_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB17_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB18_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB19_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB20_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB21_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB22_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB23_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB24_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB25_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB26_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB27_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB28_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB29_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB30_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void GPIOB31_Handler(void)    __attribute__((weak, alias("Default_Handler")));


const IRQ_Fun RamVectorTableApp[] __attribute__((used)) __attribute__((section(".vectors_table"))) =
{
    (IRQ_Fun)0x2C0000,
    (IRQ_Fun)Reset_Handler,
    NMI_Handler,                /* -14 NMI Handler */
    HardFault_Handler,          /* -13 Hard Fault Handler */
    MemManage_Handler,          /* -12 MPU Fault Handler */
    BusFault_Handler,           /* -11 Bus Fault Handler */
    UsageFault_Handler,         /* -10 Usage Fault Handler */
    0, 0, 0, 0,                 /* Reserved */
    SVC_Handler,                /* -5 SVCall Handler */
    DebugMon_Handler,           /* -4 Debug Monitor Handler */
    0,                          /* Reserved */
    PendSV_Handler,             /* -2 PendSV Handler */
    SysTick_Handler,            /* -1 SysTick Handler */

    // External Interrupts
    System_Handler,
    WDT_Handler,
    BTMAC_Handler,
    DSP_Handler,
    RXI300_Handler,
    SPI0_Handler,
    I2C0_Handler,
    ADC_Handler,
    SPORT0_TX_Handler,
    SPORT0_RX_Handler,
    TIM2_Handler,
    TIM3_Handler,
    TIM4_Handler,
    RTC_Handler,
    UART0_Handler,
    UART1_Handler,
    UART2_Handler,
    Peri_Handler,
    GPIO_A0_Handler,
    GPIO_A1_Handler,
    GPIO_A_2_7_Handler,
    GPIO_A_8_15_Handler,
    GPIO_A_16_23_Handler,
    GPIO_A_24_31_Handler,
    SPORT1_RX_Handler,
    SPORT1_TX_Handler,
    ADP_DET_Handler,
    VBAT_DET_Handler,
    GDMA0_Channel0_Handler,
    GDMA0_Channel1_Handler,
    GDMA0_Channel2_Handler,
    GDMA0_Channel3_Handler,
    GDMA0_Channel4_Handler,
    GDMA0_Channel5_Handler,
    GDMA0_Channel6_Handler,
    GDMA0_Channel7_Handler,
    GDMA0_Channel8_Handler,
    GPIO_B_0_7_Handler,
    GPIO_B_8_15_Handler,
    GPIO_B_16_23_Handler,
    GPIO_B_24_31_Handler,
    SPI1_Handler,
    SPI2_Handler,
    I2C1_Handler,
    I2C2_Handler,
    KeyScan_Handler,
    QDEC_Handler,
    USB_Handler,
    USB_ISOC_Handler,
    SPIC0_Handler,
    SPIC1_Handler,
    SPIC2_Handler,
    SPIC3_Handler,
    TIM5_Handler,
    TIM6_Handler,
    TIM7_Handler,
    ASRC0_Handler,
    ASRC1_Handler,
    I8080_Handler,
    ISO7816_Handler,
    SDIO0_Handler,
    SPORT2_RX_Handler,
    ANC_Handler,
    TOUCH_Handler,
    GDMA0_Channel9_Handler,
    GDMA0_Channel10_Handler,
    GDMA0_Channel11_Handler,
    Display_Handler,
    PPE_Handler,
    IMDC_Handler,
    Slave_Port_Monitor_Handler,
    RTK_Timer0_Handler,
    RTK_Timer1_Handler,
    RTK_Timer2_Handler,
    RTK_Timer3_Handler,
    CAN_Handler,
    BTMAC_WRAP_AROUND_Handler,
    SHA256_Handler,
    Public_Key_Engine_Handler,
    SPI_PHY1_INTR_Handler,
    MFB_DET_L_Handler,
    PTA_Mailbox_Handler,
    Utmi_Suspend_N_Handler,
    IR_Handler,
    TRNG_Handler,
    PSRAMC_Handler,
    ADP_IN_DET_Handler,
    ADP_OUT_DET_Handler,
    GPIOA2_Handler,
    GPIOA3_Handler,
    GPIOA4_Handler,
    GPIOA5_Handler,
    GPIOA6_Handler,
    GPIOA7_Handler,
    GPIOA8_Handler,
    GPIOA9_Handler,
    GPIOA10_Handler,
    GPIOA11_Handler,
    GPIOA12_Handler,
    GPIOA13_Handler,
    GPIOA14_Handler,
    GPIOA15_Handler,
    GPIOA16_Handler,
    GPIOA17_Handler,
    GPIOA18_Handler,
    GPIOA19_Handler,
    GPIOA20_Handler,
    GPIOA21_Handler,
    GPIOA22_Handler,
    GPIOA23_Handler,
    GPIOA24_Handler,
    GPIOA25_Handler,
    GPIOA26_Handler,
    GPIOA27_Handler,
    GPIOA28_Handler,
    GPIOA29_Handler,
    GPIOA30_Handler,
    GPIOA31_Handler,
    GPIOB0_Handler,
    GPIOB1_Handler,
    GPIOB2_Handler,
    GPIOB3_Handler,
    GPIOB4_Handler,
    GPIOB5_Handler,
    GPIOB6_Handler,
    GPIOB7_Handler,
    GPIOB8_Handler,
    GPIOB9_Handler,
    GPIOB10_Handler,
    GPIOB11_Handler,
    GPIOB12_Handler,
    GPIOB13_Handler,
    GPIOB14_Handler,
    GPIOB15_Handler,
    GPIOB16_Handler,
    GPIOB17_Handler,
    GPIOB18_Handler,
    GPIOB19_Handler,
    GPIOB20_Handler,
    GPIOB21_Handler,
    GPIOB22_Handler,
    GPIOB23_Handler,
    GPIOB24_Handler,
    GPIOB25_Handler,
    GPIOB26_Handler,
    GPIOB27_Handler,
    GPIOB28_Handler,
    GPIOB29_Handler,
    GPIOB30_Handler,
    GPIOB31_Handler,
};


__attribute__((naked)) void __user_setup_stackheap(void)
{
    __asm volatile(
        "BX      lr                        \n"
    );

}

// Declare symbols imported from the linker script
extern uint32_t Load$$RAM_GLOBAL$$RW$$Base;
extern uint32_t Image$$RAM_GLOBAL$$RW$$Base;
extern uint32_t Image$$RAM_GLOBAL$$RW$$Length;

extern uint32_t Image$$RAM_GLOBAL$$ZI$$Base;
extern uint32_t Image$$RAM_GLOBAL$$ZI$$Length;

extern uint32_t Load$$RAM_TEXT$$RW$$Base;
extern uint32_t Image$$RAM_TEXT$$RW$$Base;
extern uint32_t Image$$RAM_TEXT$$RW$$Length;

extern uint32_t Load$$RAM_TEXT$$RO$$Base;
extern uint32_t Image$$RAM_TEXT$$RO$$Base;
extern uint32_t Image$$RAM_TEXT$$RO$$Length;

extern uint32_t Image$$RAM_TEXT$$ZI$$Base;
extern uint32_t Image$$RAM_TEXT$$ZI$$Length;

// Function to initialize global variables
static void initialize_memory(void)
{
    // Copy data to RW section
    {
        uint32_t *load_addr = &Load$$RAM_GLOBAL$$RW$$Base;
        uint32_t *exec_addr = &Image$$RAM_GLOBAL$$RW$$Base;
        uint32_t length = (uint32_t)&Image$$RAM_GLOBAL$$RW$$Length;

        memcpy(exec_addr, load_addr, length);
    }

    // Clear ZI section
    {
        uint32_t *exec_addr = &Image$$RAM_GLOBAL$$ZI$$Base;
        uint32_t length = (uint32_t)&Image$$RAM_GLOBAL$$ZI$$Length;

        memset(exec_addr, 0, length);
    }

    // Copy buffer to RW section
    {
        uint32_t *load_addr = &Load$$RAM_TEXT$$RW$$Base;
        uint32_t *exec_addr = &Image$$RAM_TEXT$$RW$$Base;
        uint32_t length = (uint32_t)&Image$$RAM_TEXT$$RW$$Length;

        memcpy(exec_addr, load_addr, length);
    }

    // Copy buffer to RO section (for RAM function)
    {
        uint32_t *load_addr = &Load$$RAM_TEXT$$RO$$Base;
        uint32_t *exec_addr = &Image$$RAM_TEXT$$RO$$Base;
        uint32_t length = (uint32_t)&Image$$RAM_TEXT$$RO$$Length;

        memcpy(exec_addr, load_addr, length);
    }

    // Clear ZI section for buffers
    {
        uint32_t *exec_addr = &Image$$RAM_TEXT$$ZI$$Base;
        uint32_t length = (uint32_t)&Image$$RAM_TEXT$$ZI$$Length;

        memset(exec_addr, 0, length);
    }
}

bool Reset_Handler(void)
{
    initialize_memory();
    extern bool system_init(void);
    system_init();

    extern int main(void);
    main();

    return true;
}

/*----------------------------------------------------------------------------
  Default Handler for Exceptions / Interrupts
 *----------------------------------------------------------------------------*/
void Default_Handler(void)
{
    DBG_DIRECT("Error! Please implement your ISR Handler for IRQ number %d!", __get_IPSR());
    while (1);
}
