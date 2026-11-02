/********************************** (C) COPYRIGHT  *******************************
* File Name          : ch32h417_crc.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2025/03/01
* Description        : This file provides all the CRC firmware functions.
*********************************************************************************
* Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include "hardware.h"
#include "ch32h417_usbss_device.h"
#include "ch32h417_usbhs_device.h"
#include "hd3ss3220.h"
#include "usbss_uart1_bridge.h"
#include "usb_ep3_debug.h"
#include "usb_desc.h"
#include "vcxo_discipline.h"
#include "fw_version.h"
#include "ocfw.h"

static volatile uint8_t Ocfw_EnterBoot;

/*********************************************************************
 * @fn      Ocfw_Setup
 *
 * @brief   Firmware update vendor requests (ocfw.h), interrupt context.
 *          The application answers INFO and ENTER_BOOT; the flash write
 *          and reset for ENTER_BOOT are deferred to Ocfw_App_Task() so the
 *          status stage completes first. Bootloader-only requests STALL.
 *
 * @return  1 to accept, 0 to STALL
 */
uint8_t Ocfw_Setup( uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint16_t length,
                    uint8_t *in, uint16_t *in_len )
{
    (void)index;
    if(type == 0xC0 && request == OCFW_REQ_INFO && length)
    {
        ocfw_fill_info((ocfw_info_t *)in, OCFW_MODE_APP);
        *in_len = sizeof(ocfw_info_t);
        return 1;
    }
    if(type == 0x40 && request == OCFW_REQ_ENTER_BOOT && value == OCFW_KEY && length == 0)
    {
        Ocfw_EnterBoot = 1;
        return 1;
    }
    return 0;
}

/* The application accepts no OUT data stages. */
uint8_t *Ocfw_OutBuffer( void )
{
    static uint8_t unused[4];
    return unused;
}

void Ocfw_OutDone( uint16_t length )
{
    (void)length;
}

/*********************************************************************
 * @fn      Ocfw_App_Task
 *
 * @brief   Sets the boot request flag and resets; stage-0 then starts the
 *          bootloader.
 *
 * @return  none
 */
void Ocfw_App_Task( void )
{
    if(!Ocfw_EnterBoot)
    {
        return;
    }
    Ocfw_EnterBoot = 0;
    Delay_Ms(20);
    printf("Entering bootloader\r\n");
    if(ocfw_flag_write(OCFW_FLAG_MAGIC) == 0)
    {
        Delay_Ms(5);
        ocfw_system_reset();
    }
    printf("Boot flag write failed\r\n");
}
/*********************************************************************
 * @fn      USART1_Init
 *
 * @brief   Initializes the USART1 peripheral.
 *
 * @param   baudrate - USART communication baud rate.
 *
 * @return  none
 */
void USART1_Init(uint32_t baudrate)
{
    UART1_Bridge_Init(baudrate);
} 

 /*********************************************************************
 * @fn      Hardware
 *
 * @brief   Resets the CRC Data register (DR).
 *
 * @param   none
 *
 * @return  none
 */
void Hardware( void )
{ 
    /* PA4 now drives the VCTCXO tune node (shared with ADF4002 CP): keep it
     * high-Z from the first instruction until the Zynq selects mode 2. */
    VCXO_Init();

    printf("OmniCore bridge " OMNICORE_BRIDGE_VERSION_STR " (" OMNICORE_BUILD_ID ") app_status=0x%02x boot=0x%06x\r\n",
           (unsigned)ocfw_refresh(1), (unsigned)ocfw_boot_version());

    /* Disable SWD */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO, ENABLE);
    Delay_Ms(3000);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE); //注释掉用于调试

    /* Enable the IO function of PC6 pins */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_SWPMI, ENABLE);
    SWPMI->OR |= SWPMI_SWP_TBYP;

    USART1_Init(UART1_BRIDGE_BAUD);

    /* 先初始化 USB */
    printf("USB Init\r\n");
    USB_SerialNumber_Init();
    USB_Timer_Init();
    USBSS_Device_Init(ENABLE);

    /* Either controller may complete enumeration; keep servicing UART/logs. */
    printf("Wait USB enumeration...\r\n");
    while(USBSS_DevEnumStatus == 0 && USBHS_DevEnumStatus == 0)
    {
        UART1_Bridge_Task();
        VCXO_Task();
        USB_Debug_Task();
        Delay_Ms(1);
    }

    /* 枚举完成后，再初始化并启动 UHSIF */
    printf("USB%u ready, UHSIF Init\r\n", USBSS_DevEnumStatus ? 3U : 2U);
    UHSIF_Init();

    #include "hd3ss3220.h"

    HD3SS3220_Result ret;
    HD3SS3220_Status typec_status;

    ret = HD3SS3220_Init();

    if (ret == HD3SS3220_OK)
    {
        ret = HD3SS3220_PrintInfo();  /* 读取并打印芯片信息和状态 */

        /* The 3 A indicator LED on PA4 was removed: PA4 is the VCXO DAC. */
        ret = HD3SS3220_ReadStatus(&typec_status);
        (void)typec_status;
    }


	while (1)
    {
        UART_Proc();
    }
		
}
 /*********************************************************************
 * @fn      UART_Proc
 *
 * @brief   Serial port-related code.
 *
 * @param   none
 *
 * @return  none
 */
void UART_Proc( void )
{
    UART1_Bridge_Task();
    VCXO_Task();
    USB_Debug_Task();
    Ocfw_App_Task();
}