
#ifndef CH32H417_IP_H
#define CH32H417_IP_H


/****************** Include Files ********************/
#include "xil_types.h"
#include "xstatus.h"
#include "xil_io.h"

#define CH32H417_IP_S00_AXI_SLV_REG0_OFFSET 0
#define CH32H417_IP_S00_AXI_SLV_REG1_OFFSET 4
#define CH32H417_IP_S00_AXI_SLV_REG2_OFFSET 8
#define CH32H417_IP_S00_AXI_SLV_REG3_OFFSET 12


/**************************** Type Definitions *****************************/
/**
 *
 * Write a value to a CH32H417_IP register. A 32 bit write is performed.
 * If the component is implemented in a smaller width, only the least
 * significant data is written.
 *
 * @param   BaseAddress is the base address of the CH32H417_IPdevice.
 * @param   RegOffset is the register offset from the base to write to.
 * @param   Data is the data written to the register.
 *
 * @return  None.
 *
 * @note
 * C-style signature:
 * 	void CH32H417_IP_mWriteReg(u32 BaseAddress, unsigned RegOffset, u32 Data)
 *
 */
#define CH32H417_IP_mWriteReg(BaseAddress, RegOffset, Data) \
  	Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))

/**
 *
 * Read a value from a CH32H417_IP register. A 32 bit read is performed.
 * If the component is implemented in a smaller width, only the least
 * significant data is read from the register. The most significant data
 * will be read as 0.
 *
 * @param   BaseAddress is the base address of the CH32H417_IP device.
 * @param   RegOffset is the register offset from the base to write to.
 *
 * @return  Data is the data from the register.
 *
 * @note
 * C-style signature:
 * 	u32 CH32H417_IP_mReadReg(u32 BaseAddress, unsigned RegOffset)
 *
 */
#define CH32H417_IP_mReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))

/* Legacy compatibility entry point: returns XST_NO_FEATURE without MMIO.
 * The generated scratch-register test is invalid for this streaming core.
 */
XStatus CH32H417_IP_Reg_SelfTest(void * baseaddr_p);

/* Revision 3 register map. CTRL registers are RW; STATUS registers are RO. */
#define CH32H417_IP_CTRL0  0U
#define CH32H417_IP_CTRL1  4U
#define CH32H417_IP_STATUS0 8U
#define CH32H417_IP_STATUS1 12U
#define CH32H417_IP_RX_EN   (1U << 0)
#define CH32H417_IP_TX_EN   (1U << 1)
#define CH32H417_IP_RX_DUAL (1U << 2)
#define CH32H417_IP_RX_CH1  (1U << 3)
#define CH32H417_IP_TX_DUAL (1U << 4)
#define CH32H417_IP_TX_CH1  (1U << 5)
#define CH32H417_IP_TX_RUN  (1U << 6)
/* IQ8 extension: the transport FIFO remains 96 bits (two dual-channel
 * frames). STATUS0[29] is zero on older FPGA builds; never infer support from
 * a writable CTRL0 bit. A mode change flushes both directions while stopped. */
#define CH32H417_IP_IQ8 (1U << 7)
#define CH32H417_IP_IQ8_APPLIED (1U << 28)
#define CH32H417_IP_IQ8_CAPABLE (1U << 29)
/* IP 1.3: time base and framed transport only. STATUS0[30] reports the time
 * capability, STATUS0[31] (framed) is always 1. Every 16 KiB UHSIF block
 * carries a 16-byte header and 16368 bytes of payload (whole samples).
 * CTRL0 bit 8 (FRAMED in 1.2) is reserved and ignored. RX_TIMED: after RX_EN
 * the receiver waits for CH32H417_IP_RxCommand; change it only with RX
 * stopped (Configure sequences this automatically). */
#define CH32H417_IP_FRAMED   (1U << 8)
#define CH32H417_IP_RX_TIMED (1U << 9)
#define CH32H417_IP_FRAMED_APPLIED (1U << 31)
#define CH32H417_IP_TIME_CAPABLE   (1U << 30)
#define CH32H417_IP_RX_TIMED_APPLIED (1U << 31) /* STATUS1 */
#define CH32H417_IP_CONFIG_MASK 0x3FFU
#define CH32H417_IP_TIME_SET_LO   0x10U
#define CH32H417_IP_TIME_SET_HI   0x14U
#define CH32H417_IP_TIME_CTRL     0x18U
#define CH32H417_IP_TIME_STATUS   0x1CU
#define CH32H417_IP_TIME_NOW_LO   0x20U
#define CH32H417_IP_TIME_NOW_HI   0x24U
#define CH32H417_IP_TIME_PPS_LO   0x28U
#define CH32H417_IP_TIME_PPS_HI   0x2CU
#define CH32H417_IP_RX_CMD_TIME_LO 0x30U
#define CH32H417_IP_RX_CMD_TIME_HI 0x34U
#define CH32H417_IP_RX_CMD_NSAMP  0x38U
#define CH32H417_IP_RX_CMD        0x3CU
#define CH32H417_IP_EVT_STATUS    0x40U
#define CH32H417_IP_EVT_INFO      0x44U
#define CH32H417_IP_EVT_TIME_LO   0x48U
#define CH32H417_IP_EVT_TIME_HI   0x4CU
#define CH32H417_IP_PPS_COUNT     0x50U
#define CH32H417_IP_VERSION       0x54U
#define CH32H417_IP_FRAME_INFO    0x58U
#define CH32H417_IP_TIME_NOW      1U
#define CH32H417_IP_TIME_NEXT_PPS 2U
#define CH32H417_IP_TIME_INVALIDATE 3U
#define CH32H417_IP_PPS_NONE      0U
#define CH32H417_IP_PPS_EXTERNAL  1U
#define CH32H417_IP_PPS_INTERNAL  2U
#define CH32H417_IP_RX_CMD_NOW    1U
#define CH32H417_IP_RX_CMD_STOP   2U
/* Event codes (EVT_INFO[31:24]). Times are sample ticks. */
#define CH32H417_IP_EV_TX_ACK       0x01U
#define CH32H417_IP_EV_TX_UNDERFLOW 0x02U
#define CH32H417_IP_EV_TX_LATE      0x03U
#define CH32H417_IP_EV_TX_SEQ       0x04U
#define CH32H417_IP_EV_TX_HEADER    0x05U
#define CH32H417_IP_EV_RX_OVERFLOW  0x10U
#define CH32H417_IP_EV_RX_LATE      0x11U
#define CH32H417_IP_EV_RX_CMD_IGNORED 0x12U
#define CH32H417_IP_CFG_BUSY   (1U << 14)
#define CH32H417_IP_CLEAR_BUSY (1U << 15)
#define CH32H417_IP_RX_READY   ((1U << 16) | (1U << 18))
#define CH32H417_IP_TX_READY   ((1U << 17) | (1U << 19))
#define CH32H417_IP_CFG_ERROR  (1U << 27)
#define CH32H417_IP_ERROR_MASK 0xFFU
#define CH32H417_IP_ERR_ADC_OVERFLOW  (1U << 0)
#define CH32H417_IP_ERR_RX_UNDERFLOW  (1U << 1)
#define CH32H417_IP_ERR_TX_OVERFLOW  (1U << 2)
#define CH32H417_IP_ERR_DAC_UNDERFLOW (1U << 3)
#define CH32H417_IP_ERR_RX_PROTOCOL  (1U << 4)
#define CH32H417_IP_ERR_TX_PROTOCOL  (1U << 5)
#define CH32H417_IP_ERR_ADC_PAIR     (1U << 6)
#define CH32H417_IP_ERR_DAC_PAIR     (1U << 7)

#define CH32H417_IP_OK       0
#define CH32H417_IP_INVALID -1
#define CH32H417_IP_TIMEOUT -2
#define CH32H417_IP_DEFAULT_TIMEOUT_US 100000U

typedef struct {
    UINTPTR BaseAddress;
    u32 TimeoutUs;
} CH32H417_IP;

typedef struct {
    u64 Now, LastPps;
    u32 PpsCount, Flags; /* Flags = TIME_STATUS register */
} CH32H417_IP_Time;

typedef struct {
    u32 Code, Lost, Seq;
    u64 Time;
} CH32H417_IP_Event;

typedef struct {
    u32 RequestedConfig;
    u32 AppliedConfig;
    u32 Status0;
    u32 Status1;
    u32 RxGroups;
    u32 TxGroups;
} CH32H417_IP_Status;

/* Single-caller API (serialize calls if used from interrupts/tasks).
 * Initialize only binds the address; it does not enable or reset streaming.
 * All three stream clocks must run for configuration/clear handshakes.
 * Configure stops/resets only directions whose mode/channel changes, waits
 * for their ready flags to clear, changes mode while disabled, then enables.
 * IQ8 requires both DUAL bits and hardware capability; format transitions
 * stop/reset both directions before changing the shared wire format.
 * Stopping/changing mode discards buffered data. Quiesce the CH32 host first.
 * A timeout may leave a partially applied configuration: inspect status before
 * retrying. It is not an automatic rollback. TX_RUN waits for FIFO prefill in
 * hardware; configuration completion does not mean TX_STARTED is asserted.
 */
int CH32H417_IP_Initialize(CH32H417_IP *Instance, UINTPTR BaseAddress,
                         u32 TimeoutUs);
int CH32H417_IP_GetStatus(const CH32H417_IP *Instance,
                        CH32H417_IP_Status *Status);
int CH32H417_IP_GetConfig(const CH32H417_IP *Instance, u32 *Config);
int CH32H417_IP_Configure(CH32H417_IP *Instance, u32 Config);
int CH32H417_IP_Stop(CH32H417_IP *Instance);
/* Waits for clear acknowledgement, not for errors==0 (errors can recur). */
int CH32H417_IP_ClearErrors(CH32H417_IP *Instance);
/* Time base. Code: TIME_NOW, TIME_NEXT_PPS, TIME_INVALIDATE or 0 (only the
 * PPS source). Returns after the radio domain acknowledged the command. */
int CH32H417_IP_TimeCommand(CH32H417_IP *Instance, u32 Code, u32 PpsSource, u64 Value);
int CH32H417_IP_GetTime(const CH32H417_IP *Instance, CH32H417_IP_Time *Time);
/* Timed RX command (FRAMED + RX_TIMED): Flags RX_CMD_NOW/RX_CMD_STOP,
 * NumSamples 0 = continuous. */
int CH32H417_IP_RxCommand(CH32H417_IP *Instance, u32 Flags, u64 Time, u32 NumSamples);
/* Returns 1 and pops one event, 0 when the event FIFO is empty. */
int CH32H417_IP_PopEvent(CH32H417_IP *Instance, CH32H417_IP_Event *Event);
u32 CH32H417_IP_Version(const CH32H417_IP *Instance);

#endif // CH32H417_IP_H
