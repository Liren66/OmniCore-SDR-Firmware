/********************************** (C) COPYRIGHT *******************************
 * File Name          : ch32h417_uhsif.c
 * Author             : WCH
 * Version            : V1.0.2
 * Date               : 2026/09/09
 * Description        : This file provides all the UHSIF firmware functions.
 *********************************************************************************
 * Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/
#include "ch32H417_uhsif.h"
#include "ch32h417_usbss_device.h"
#include "ch32h417_usbhs_device.h"
#include "stream_diag.h"

pack_t UHSIF_WR_Pack[ DEF_UHSIF_TXBUF_CNT ];
comm_t UHSIF_WR_COMM;
pack_t UHSIF_RD_Pack[ DEF_UHSIF_RXBUF_CNT ];
comm_t UHSIF_RD_COMM;

/* Diagnostic counters only: no logging or waits in transfer interrupts. */
volatile uint32_t StreamDiag[64];
volatile uint32_t StreamEvents[256][8];
static uint32_t stream_last[2];

/* USB packet buffers are independent of the eight UHSIF hardware sockets.
 * libUHSIF.a (2026/09/09) exports four lines x eight descriptors x eight words.
 * Word 0 is the data address; only change it while that socket is unowned,
 * before UHSIF_Trans_Cfg reloads/enables the descriptor. No payload copies.
 * Verified library SHA256: cae5e0c617a6c6706c2e3174de4cad51e426e3c425980c8f98931933525f1195.
 */
extern volatile uint32_t SOCKET_DSCR_WR[4][8][8];
static volatile uint8_t usb_out_armed, usb_out_next;
static volatile uint8_t pool_used, pool_ready, pool_receive, pool_submit;
static volatile uint8_t stream_fault;
static uint8_t uhsif_initialized;
static uint16_t pool_length[DEF_USB_TX_POOL_COUNT];
static uint32_t stream_lock(void) {
    uint32_t key;
    __asm volatile("csrrc %0, 0x800, %1" : "=r"(key) : "r"(0x88U) : "memory");
    return key&0x88U;
}
static void stream_unlock(uint32_t key) {
    __asm volatile("csrs 0x800, %0" : : "r"(key) : "memory");
}
static void stream_submit(void) {
    while(uhsif_initialized && pool_ready && UHSIF_WR_COMM.total<DEF_UHSIF_TXBUF_CNT && !stream_fault) {
        uint8_t slot=UHSIF_WR_COMM.load;
        uint32_t address=(uint32_t)&USBSS_EP1_Rx_Buf[pool_submit*DEF_BUFF_SIZE];
        SOCKET_DSCR_WR[DEF_LINE1][slot][0]=address;
        __asm volatile("fence iorw, iorw" ::: "memory");
        UHSIF_WR_Pack[slot].adr=address;
        UHSIF_WR_Pack[slot].len=pool_length[pool_submit];
        UHSIF_WR_COMM.total++;
        if(UHSIF_Trans_Cfg(DEF_LINE1,slot,pool_length[pool_submit])!=DEF_UHSIF_SUCESS) {
            StreamDiag[14]++; stream_fault=1; break;
        }
        UHSIF_WR_COMM.load=(slot+1)%DEF_UHSIF_TXBUF_CNT;
        pool_submit=(pool_submit+1)%DEF_USB_TX_POOL_COUNT;
        pool_ready--;
    }
}
void UHSIF_USBOut_Refill(void) {
    uint32_t key=stream_lock();
    while(usb_out_armed<4 && pool_used<DEF_USB_TX_POOL_COUNT && !stream_fault) {
        USBSSD->EP1_RX.UEP_RX_DMA=(uint32_t)&USBSS_EP1_Rx_Buf[usb_out_next*DEF_BUFF_SIZE];
        USBSSD->EP1_RX.UEP_RX_DMA_OFS=DEF_USB_EP1_SS_SIZE;
        __asm volatile("fence iorw, iorw" ::: "memory");
        USBSSD->EP1_RX.UEP_RX_CHAIN_MAX_NUMP=DEF_ENDP1_OUT_BURST_LEVEL;
        usb_out_next=(usb_out_next+1)%DEF_USB_TX_POOL_COUNT;
        usb_out_armed++; pool_used++;
    }
    UHSIF_WR_COMM.stop=(usb_out_armed==0);
    StreamDiag[10]=usb_out_armed | (pool_ready<<8) | (pool_used<<16) | (stream_fault<<24);
    stream_unlock(key);
}
void UHSIF_USBOut_Reset(void) {
    usb_out_armed=usb_out_next=0;
    pool_used=pool_ready=pool_receive=pool_submit=stream_fault=0;
    UHSIF_WR_COMM.load=UHSIF_WR_COMM.deal=UHSIF_WR_COMM.total=UHSIF_WR_COMM.stop=0;
    UHSIF_USBOut_Refill();
}
void UHSIF_USBOut_Complete(uint32_t length) {
    uint32_t key=stream_lock();
    if(usb_out_armed) usb_out_armed--; else {StreamDiag[11]++;stream_fault=1;}
    if(length!=DEF_BUFF_SIZE) {StreamDiag[13]++;stream_fault=1;}
    pool_length[pool_receive]=length;
    pool_receive=(pool_receive+1)%DEF_USB_TX_POOL_COUNT;
    pool_ready++;
    USBSSD->EP1_RX.UEP_RX_CHAIN_ST=USBSS_EP_RX_CHAIN_IF;
    stream_submit();
    UHSIF_USBOut_Refill();
    stream_unlock(key);
}
/* Call only after the host has stopped FPGA RX/TX and cancelled EP1 transfers.
 * Clear chain state, retaining the USB packet sequence numbers. */
void UHSIF_Stream_Reset(void) {
    uint32_t key=stream_lock();
    uint8_t restart=uhsif_initialized;
    if(restart) UHSIF_Start(DISABLE);
    uhsif_initialized=0;
    USBSSD->EP1_RX.UEP_RX_CR=USBSS_EP_RX_CHAIN_CLR | DEF_ENDP1_OUT_BURST_LEVEL;
    USBSSD->EP1_TX.UEP_TX_CR=USBSS_EP_TX_CHAIN_CLR | DEF_ENDP1_IN_BURST_LEVEL;
    UHSIF_USBOut_Reset();
    if(restart) UHSIF_Init();
    USBSSD->EP1_TX.UEP_TX_DMA=(uint32_t)USBSS_EP1_Tx_Buf;
    USBSSD->EP1_TX.UEP_TX_CHAIN_LEN=DEF_USB_EP1_SS_SIZE;
    stream_unlock(key);
}

/* USBHS burst engine uses the same 16 KiB IQ buffers as USBSS.
 * Endpoint enable gates flush partial bursts; preserve DATA0/DATA1 on the
 * vendor cleanup request (SET_CONFIGURATION resets toggles separately). */
void UHSIF_Stream_ResetHS(void)
{
    uint32_t key = stream_lock();
    uint8_t restart = uhsif_initialized;
    uint8_t rx_toggle = USBHSD->UEP1_RX_CTRL & USBHS_UEP_R_TOG_MASK;
    uint8_t tx_toggle = USBHSD->UEP1_TX_CTRL & USBHS_UEP_T_TOG_MASK;
    USBHSD->UEP1_RX_CTRL = rx_toggle | USBHS_UEP_R_RES_NAK;
    USBHSD->UEP1_TX_CTRL = tx_toggle | USBHS_UEP_T_RES_NAK;
    USBHSD->UEP_RX_EN &= ~USBHS_UEP1_R_EN;
    USBHSD->UEP_TX_EN &= ~USBHS_UEP1_T_EN;
    if(restart) UHSIF_Start(DISABLE);
    uhsif_initialized = 0;
    usb_out_armed = usb_out_next = 0;
    pool_used = pool_ready = pool_receive = pool_submit = stream_fault = 0;
    UHSIF_Para_Init();
    USBHSD->UEP1_RX_DMA = (uint32_t)USBSS_EP1_Rx_Buf;
    USBHSD->UEP1_TX_DMA = (uint32_t)USBSS_EP1_Tx_Buf;
    USBHSD->UEP1_RX_SIZE = DEF_BUFF_SIZE;
    USBHSD->UEP1_RX_LEN = 0;
    USBHSD->UEP1_TX_LEN = 0;
    USBHSD->UEP_RX_EN |= USBHS_UEP1_R_EN;
    USBHSD->UEP_TX_EN |= USBHS_UEP1_T_EN;
    if(restart) UHSIF_Init();
    USBHSD->UEP1_RX_CTRL = rx_toggle |
        ((restart && USBHS_DevEnumStatus) ? USBHS_UEP_R_RES_ACK : USBHS_UEP_R_RES_NAK);
    USBHSD->UEP1_TX_CTRL = tx_toggle | USBHS_UEP_T_RES_NAK;
    stream_unlock(key);
}

void StreamDiag_Event(uint32_t kind) {
    uint32_t now=stream_cycles(), delta=now-stream_last[kind];
    uint32_t base=32+kind*8;
    StreamDiag[base]++;
    if(stream_last[kind] && delta < SystemCoreClock/100) {
        if(delta>StreamDiag[base+1]) {
            StreamDiag[base+1]=delta;
            StreamDiag[base+2]=UHSIF_WR_COMM.total;
            StreamDiag[base+3]=UHSIF_WR_COMM.stop;
        }
        if(delta>SystemCoreClock/20000) {
            uint32_t seq=StreamDiag[4]++;
            volatile uint32_t *e=StreamEvents[seq&255];
            e[0]=seq; e[1]=kind; e[2]=now; e[3]=delta;
            e[4]=UHSIF_WR_COMM.total | (UHSIF_WR_COMM.stop<<8) |
                 (UHSIF_WR_COMM.load<<16) | (UHSIF_WR_COMM.deal<<24);
            e[5]=USBHS_DevEnumStatus ? USBHSD->MIS_ST : USBSSD->LINK_STATUS;
            e[6]=USBHS_DevEnumStatus ? USBHSD->UEP1_RX_CTRL : USBSSD->EP1_RX.UEP_RX_ST;
            e[7]=USBHS_DevEnumStatus ? USBHSD->UEP1_RX_LEN : USBSSD->EP1_RX.UEP_RX_CHAIN_MAX_NUMP;
            StreamDiag[base+4]++;
        }
    }
    stream_last[kind]=now;
    if(kind==1 && UHSIF_WR_COMM.total==1) StreamDiag[base+5]++;
}
uint16_t StreamDiag_Read(uint8_t *out,uint16_t page,uint16_t reset) {
    uint32_t i,key;
    __asm volatile("csrrc %0, 0x800, %1" : "=r"(key) : "r"(0x88U) : "memory");
    if(reset==2) {
        if(USBHS_DevEnumStatus) UHSIF_Stream_ResetHS();
        else UHSIF_Stream_Reset();
    }
    if(reset==1) {
        for(i=0;i<64;i++) StreamDiag[i]=0;
        stream_last[0]=stream_last[1]=0;
    }
    StreamDiag[0]=0x53444731; StreamDiag[1]=4;
    StreamDiag[2]=SystemCoreClock; StreamDiag[3]=stream_cycles();
    StreamDiag[5]=UHSIF_WR_COMM.total | (UHSIF_WR_COMM.stop<<8) |
                  (UHSIF_WR_COMM.load<<16) | (UHSIF_WR_COMM.deal<<24);
    StreamDiag[6]=USBHS_DevEnumStatus ? USBHSD->MIS_ST : USBSSD->LINK_STATUS;
    StreamDiag[7]=USBHS_DevEnumStatus ? USBHSD->CONTROL : USBSSD->LINK_CFG;
    if(page==0) {
        for(i=0;i<64;i++) ((uint32_t*)out)[i]=StreamDiag[i];
    } else if(page<=16) {
        for(i=0;i<128;i++) ((uint32_t*)out)[i]=((volatile uint32_t*)StreamEvents)[(page-1)*128+i];
    }
    __asm volatile("csrs 0x800, %0" : : "r"(key&0x88U) : "memory");
    return page==0 ? 256 : (page<=16 ? 512 : 0);
}


/*******************************************************************************
 * @fn      UHSIF_Init
 *
 * @brief   UHSIF initialization.
 *
 * @param   None
 *
 * @return  None
 */
 void UHSIF_Init( void )
 {
    uint32_t key=stream_lock();
    UHSIF_Para_Init( );

    /* For the UHSIF pin initialization, the host needs to configure the frequency division coefficient */
    // UHSIF_GPIO_Init( DEF_UHSIF_SLAVE_FPGA_MODE, DEF_UHSIF_PINREMAP2, DEF_UHSIF_CLKREMAP1, 0, DEF_UHSIF_DATA_BIT32 );
    // UHSIF_GPIO_Init( DEF_UHSIF_SLAVE_SOC_MODE, DEF_UHSIF_PINREMAP2, DEF_UHSIF_CLKREMAP1, 0 , DEF_UHSIF_DATA_BIT32 ); 
    UHSIF_GPIO_Init( DEF_UHSIF_MASTER_MODE, DEF_UHSIF_PINREMAP2, DEF_UHSIF_CLKREMAP1, RCC_UHSIFDIV_DIV3, DEF_UHSIF_DATA_BIT32 );

    /* Initialization of UHSIF-related registers */  
    UHSIF_Cfg( );
    
    /* UHSIF line and buff initialization */  
    UHSIF_Line_Cfg( DEF_LINE0, DEF_LINE_DIR_IN, DEF_BUFF_COUNT_8, USBSS_EP1_Tx_Buf, 0);
    UHSIF_Line_Cfg( DEF_LINE1, DEF_LINE_DIR_OUT, DEF_BUFF_COUNT_8, USBSS_EP1_Rx_Buf, 0 );

    // UHSIF_Line_Cfg( DEF_LINE0, DEF_LINE_DIR_IN, DEF_BUFF_COUNT_8, USBSS_EP1_Tx_Buf, 254 );
    // UHSIF_Line_Cfg( DEF_LINE1, DEF_LINE_DIR_OUT, DEF_BUFF_COUNT_8, USBSS_EP1_Rx_Buf, 5 );

    /* UHSIF enabled */  
    UHSIF_Start( ENABLE );
    uhsif_initialized=1;
    if(USBHS_DevEnumStatus)
    {
        USBHSD->UEP1_RX_DMA = UHSIF_WR_Pack[0].adr;
        USBHSD->UEP1_TX_DMA = UHSIF_RD_Pack[0].adr;
        __asm volatile("fence iorw, iorw" ::: "memory");
        USBHSD->UEP1_RX_CTRL = (USBHSD->UEP1_RX_CTRL & USBHS_UEP_R_TOG_MASK) | USBHS_UEP_R_RES_ACK;
    }
    else stream_submit();
    stream_unlock(key);
 }

/*******************************************************************************
 * @fn      UHSIF_Para_Init
 *
 * @brief   UHSIF-Related initialization.
 *
 * @param   None
 *
 * @return  None
 */
void UHSIF_Para_Init( void )
{
    uint8_t i;

    UHSIF_WR_COMM.load = 0;
    UHSIF_WR_COMM.deal = 0;
    UHSIF_WR_COMM.total = 0;
    UHSIF_WR_COMM.stop = 0;

    UHSIF_RD_COMM.load = 0;
    UHSIF_RD_COMM.deal = 0;
    UHSIF_RD_COMM.total = 0;
    UHSIF_RD_COMM.stop = 0;

    for( i = 0; i < DEF_UHSIF_TXBUF_CNT; i++ )
    {
        UHSIF_WR_Pack[ i ].adr = (uint32_t)&USBSS_EP1_Rx_Buf[ DEF_USB_EP1_SS_SIZE * DEF_ENDP1_OUT_BURST_LEVEL * i ];
        UHSIF_WR_Pack[ i ].len = 0;
    }

    for( i = 0; i < DEF_UHSIF_RXBUF_CNT; i++ )
    {
        UHSIF_RD_Pack[ i ].adr = (uint32_t)&USBSS_EP1_Tx_Buf[ DEF_USB_EP1_SS_SIZE * DEF_ENDP1_OUT_BURST_LEVEL * i ];
        UHSIF_RD_Pack[ i ].len = 0;
    }
}

/*******************************************************************************
 * @fn      UHSIF_Line0_IN_Callback
 *
 * @brief   The interrupt callback function for line 0 configuration input
 *
 * @param   rcv_count: The length of the data received by line 0
 *
 * @return  None
 */
void UHSIF_Line0_IN_Callback( uint32_t rcv_count )
{
    uint32_t cnt = DEF_TX_CHAIN_TIMEOUT;
    uint32_t key = stream_lock();

    if( rcv_count )
    {
        UHSIF_RD_Pack[ UHSIF_RD_COMM.load ].len = rcv_count;

        UHSIF_RD_COMM.total++;
    
        if( UHSIF_RD_COMM.total == 1 ) 
        {
            if( USBSS_DevEnumStatus )
            {
                while( cnt-- )
                {
                    if(( USBSSD->EP1_TX.UEP_TX_ST & USBSS_EP_TX_CHAIN_EN_MASK ) != USBSS_EP_TX_CHAIN_EN_MASK )
                    {
                        if( UHSIF_RD_Pack[ UHSIF_RD_COMM.load ].len % 1024 )
                        {
                            USBSSD->EP1_TX.UEP_TX_CHAIN_LEN = UHSIF_RD_Pack[ UHSIF_RD_COMM.load ].len % 1024;
                            USBSSD->EP1_TX.UEP_TX_CHAIN_EXP_NUMP = UHSIF_RD_Pack[ UHSIF_RD_COMM.load ].len / 1024 + 1;
                        }
                        else 
                        {
                            USBSSD->EP1_TX.UEP_TX_CHAIN_LEN = 1024;
                            USBSSD->EP1_TX.UEP_TX_CHAIN_EXP_NUMP = UHSIF_RD_Pack[ UHSIF_RD_COMM.load ].len / 1024;
                        }

                        break;
                    }
                }  
            }
            else if( USBHS_DevEnumStatus )
            {
                USBHSD->UEP1_TX_LEN = UHSIF_RD_Pack[ UHSIF_RD_COMM.load ].len;
                USBHSD->UEP1_TX_CTRL = (USBHSD->UEP1_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK;
            }
        }
        UHSIF_RD_COMM.load++;                                         
        if( UHSIF_RD_COMM.load >= DEF_UHSIF_RXBUF_CNT )                
        {
            UHSIF_RD_COMM.load = 0;
        }
    }
    stream_unlock(key);
}

/*******************************************************************************
 * @fn      UHSIF_Line1_IN_Callback
 *
 * @brief   The interrupt callback function for line 1 configuration input
 *
 * @param   rcv_count: The length of the data received by line 1
 *
 * @return  None
 */
void UHSIF_Line1_IN_Callback( uint32_t rcv_count )
{
    
}

/*******************************************************************************
 * @fn      UHSIF_Line2_IN_Callback
 *
 * @brief   The interrupt callback function for line 2 configuration input
 *
 * @param   rcv_count: The length of the data received by line 2
 *
 * @return  None
 */
void UHSIF_Line2_IN_Callback( uint32_t rcv_count )
{
    
}

/*******************************************************************************
 * @fn      UHSIF_Line3_IN_Callback
 *
 * @brief   The interrupt callback function for line 3 configuration input
 *
 * @param   rcv_count: The length of the data received by line 3
 *
 * @return  None
 */
void UHSIF_Line3_IN_Callback( uint32_t rcv_count )
{
    
}

/*******************************************************************************
 * @fn      UHSIF_Line0_OUT_Callback
 *
 * @brief   The interrupt callback function for line 0 configuration output
 *
 * @param   None
 *
 * @return  None
 */
void UHSIF_Line0_OUT_Callback( void )
{

}

/*******************************************************************************
 * @fn      UHSIF_Line1_OUT_Callback
 *
 * @brief   The interrupt callback function for line 1 configuration output
 *
 * @param   None
 *
 * @return  None
 */
void UHSIF_Line1_OUT_Callback( void )
{
    uint32_t key=stream_lock();
    StreamDiag_Event(1);
    UHSIF_WR_COMM.deal=(UHSIF_WR_COMM.deal+1)%DEF_UHSIF_TXBUF_CNT;
    if(UHSIF_WR_COMM.total) UHSIF_WR_COMM.total--;
    else StreamDiag[12]++;
    if(USBSS_DevEnumStatus) {
        if(pool_used) pool_used--; else {StreamDiag[12]++;stream_fault=1;}
        stream_submit();
        UHSIF_USBOut_Refill();
    }
    else if(USBHS_DevEnumStatus && UHSIF_WR_COMM.stop && UHSIF_WR_COMM.total<DEF_UHSIF_TXBUF_CNT-1) {
        UHSIF_WR_COMM.stop=0;
        USBHSD->UEP1_RX_CTRL=(USBHSD->UEP1_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_ACK;
    }
    stream_unlock(key);
}

/*******************************************************************************
 * @fn      UHSIF_Line2_OUT_Callback
 *
 * @brief   The interrupt callback function for line 2 configuration output
 *
 * @param   None
 *
 * @return  None
 */
void UHSIF_Line2_OUT_Callback( void )
{
    
}

/*******************************************************************************
 * @fn      UHSIF_Line3_OUT_Callback
 *
 * @brief   The interrupt callback function for line 3 configuration output
 *
 * @param   None
 *
 * @return  None
 */
void UHSIF_Line3_OUT_Callback( void )
{
    
}

 
