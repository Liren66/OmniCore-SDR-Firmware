/********************************** (C) COPYRIGHT *******************************
* File Name          : ch32h417_it.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2025/03/01
* Description        : Main Interrupt Service Routines.
*********************************************************************************
* Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include "ch32h417_it.h"

void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

/*********************************************************************
 * @fn      NMI_Handler
 *
 * @brief   This function handles NMI exception.
 *
 * @return  none
 */
void NMI_Handler(void)
{
  while (1)
  {
  }
}

/*********************************************************************
 * @fn      HardFault_Handler
 *
 * @brief   This function handles Hard Fault exception.
 *
 * @return  none
 */
volatile uint32_t Boot_FaultCause, Boot_FaultPc;

void HardFault_Handler(void)
{
  /* Stay here instead of resetting: a reset would restart the bootloader
   * (the boot flag or the missing application send stage-0 back here) in a
   * loop that also locks out the debugger. */
  __asm volatile("csrr %0, mcause" : "=r"(Boot_FaultCause));
  __asm volatile("csrr %0, mepc" : "=r"(Boot_FaultPc));
  while (1)
  {
  }
}


