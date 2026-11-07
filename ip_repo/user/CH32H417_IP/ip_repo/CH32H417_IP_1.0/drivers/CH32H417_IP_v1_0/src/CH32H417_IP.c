

/***************************** Include Files *******************************/
#include "CH32H417_IP.h"
#include "sleep.h"

/************************** Function Definitions ***************************/

static u32 applied_config(u32 status, u32 status1)
{
    return ((status >> 24) & 3U) | ((status >> 18) & 0x3CU) |
           ((status >> 20) & CH32H417_IP_TX_RUN) |
           ((status >> 21) & CH32H417_IP_IQ8) |
           ((status1 & CH32H417_IP_RX_TIMED_APPLIED) ? CH32H417_IP_RX_TIMED : 0U);
}

static u32 read_applied(const CH32H417_IP *ip, u32 *status0)
{
    u32 s0 = CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_STATUS0);
    u32 s1 = CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_STATUS1);
    if (status0) *status0 = s0;
    return applied_config(s0, s1);
}

static int valid_instance(const CH32H417_IP *ip)
{
    return ip != 0 && (ip->BaseAddress & 3U) == 0U && ip->TimeoutUs != 0U;
}

int CH32H417_IP_Initialize(CH32H417_IP *ip, UINTPTR base, u32 timeout)
{
    if (ip == 0 || (base & 3U) != 0U || timeout == 0U)
        return CH32H417_IP_INVALID;
    ip->BaseAddress = base;
    ip->TimeoutUs = timeout;
    return CH32H417_IP_OK;
}

int CH32H417_IP_GetStatus(const CH32H417_IP *ip, CH32H417_IP_Status *s)
{
    if (!valid_instance(ip) || s == 0)
        return CH32H417_IP_INVALID;
    s->RequestedConfig = CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_CTRL0);
    s->Status0 = CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_STATUS0);
    s->Status1 = CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_STATUS1);
    s->AppliedConfig = applied_config(s->Status0, s->Status1);
    s->RxGroups = s->Status1 & 0x1FFFU;
    s->TxGroups = (s->Status1 >> 16) & 0x1FFFU;
    /* STATUS0/1 are separate reads of a periodically refreshed snapshot.
     * Counts are diagnostic, not an atomic transaction or transfer credit. */
    return CH32H417_IP_OK;
}

static int wait_idle(const CH32H417_IP *ip, u32 mask)
{
    u32 remaining = ip->TimeoutUs;
    do {
        if (!(CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_STATUS0) & mask))
            return CH32H417_IP_OK;
        usleep(1);
    } while (--remaining);
    return CH32H417_IP_TIMEOUT;
}

int CH32H417_IP_GetConfig(const CH32H417_IP *ip, u32 *config)
{
    int rc;
    if (!valid_instance(ip) || config == 0)
        return CH32H417_IP_INVALID;
    rc = wait_idle(ip, CH32H417_IP_CFG_BUSY);
    if (rc != CH32H417_IP_OK)
        return rc;
    /* Let the periodic status snapshot catch up after the CDC acknowledgement.
     * 10 us exceeds 32 clock periods at the supported operating clocks. */
    usleep(10);
    *config = read_applied(ip, 0);
    return CH32H417_IP_OK;
}

static int write_config(const CH32H417_IP *ip, u32 config)
{
    u32 ready = 0, status, remaining = ip->TimeoutUs;
    if (config & CH32H417_IP_RX_EN) ready |= CH32H417_IP_RX_READY;
    if (config & CH32H417_IP_TX_EN) ready |= CH32H417_IP_TX_READY;
    CH32H417_IP_mWriteReg(ip->BaseAddress, CH32H417_IP_CTRL0, config);
    /* Readback orders the AXI write; the delay avoids observing pre-write busy. */
    (void)CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_CTRL0);
    usleep(1);
    do {
        u32 applied = read_applied(ip, &status);
        if (!(status & CH32H417_IP_CFG_BUSY) && applied == config &&
            (status & (CH32H417_IP_RX_READY | CH32H417_IP_TX_READY)) == ready)
            return CH32H417_IP_OK;
        usleep(1);
    } while (--remaining);
    return CH32H417_IP_TIMEOUT;
}

int CH32H417_IP_Configure(CH32H417_IP *ip, u32 config)
{
    u32 current, disabled, modes;
    int rc;
    if (!valid_instance(ip) || (config & ~CH32H417_IP_CONFIG_MASK) ||
        ((config & CH32H417_IP_TX_RUN) && !(config & CH32H417_IP_TX_EN)))
        return CH32H417_IP_INVALID;
    if ((config & CH32H417_IP_IQ8) &&
        (((config & (CH32H417_IP_RX_DUAL | CH32H417_IP_TX_DUAL)) !=
          (CH32H417_IP_RX_DUAL | CH32H417_IP_TX_DUAL)) ||
         !(CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_STATUS0) &
           CH32H417_IP_IQ8_CAPABLE)))
        return CH32H417_IP_INVALID;
    /* IP 1.3 is framed only: bit 8 is reserved and ignored. */
    config &= ~CH32H417_IP_FRAMED;
    if ((config & CH32H417_IP_RX_TIMED) &&
        !(CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_STATUS0) & CH32H417_IP_TIME_CAPABLE))
        return CH32H417_IP_INVALID;
    rc = CH32H417_IP_GetConfig(ip, &current);
    if (rc != CH32H417_IP_OK) return rc;
    disabled = current;
    if ((current ^ config) & CH32H417_IP_IQ8)
        disabled &= ~(CH32H417_IP_RX_EN | CH32H417_IP_TX_EN | CH32H417_IP_TX_RUN);
    if ((current ^ config) & (CH32H417_IP_RX_DUAL | CH32H417_IP_RX_CH1 | CH32H417_IP_RX_TIMED))
        disabled &= ~CH32H417_IP_RX_EN;
    if ((current ^ config) & (CH32H417_IP_TX_DUAL | CH32H417_IP_TX_CH1))
        disabled &= ~(CH32H417_IP_TX_EN | CH32H417_IP_TX_RUN);
    if (disabled != current) {
        rc = write_config(ip, disabled);
        if (rc != CH32H417_IP_OK) return rc;
    }
    modes = (disabled & (CH32H417_IP_RX_EN | CH32H417_IP_TX_EN | CH32H417_IP_TX_RUN)) |
            (config & (CH32H417_IP_RX_DUAL | CH32H417_IP_RX_CH1 |
                       CH32H417_IP_TX_DUAL | CH32H417_IP_TX_CH1 | CH32H417_IP_IQ8 |
                       CH32H417_IP_RX_TIMED));
    if (modes != disabled) {
        rc = write_config(ip, modes);
        if (rc != CH32H417_IP_OK) return rc;
    }
    return write_config(ip, config);
}

int CH32H417_IP_Stop(CH32H417_IP *ip)
{
    u32 config;
    int rc = CH32H417_IP_GetConfig(ip, &config);
    if (rc != CH32H417_IP_OK) return rc;
    return CH32H417_IP_Configure(ip, config &
        ~(CH32H417_IP_RX_EN | CH32H417_IP_TX_EN | CH32H417_IP_TX_RUN));
}

int CH32H417_IP_ClearErrors(CH32H417_IP *ip)
{
    int rc;
    if (!valid_instance(ip)) return CH32H417_IP_INVALID;
    rc = wait_idle(ip, CH32H417_IP_CLEAR_BUSY);
    if (rc != CH32H417_IP_OK) return rc;
    CH32H417_IP_mWriteReg(ip->BaseAddress, CH32H417_IP_CTRL1, 0U);
    (void)CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_CTRL1);
    usleep(1);
    CH32H417_IP_mWriteReg(ip->BaseAddress, CH32H417_IP_CTRL1, 1U);
    (void)CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_CTRL1);
    usleep(1);
    rc = wait_idle(ip, CH32H417_IP_CLEAR_BUSY);
    /* Return the edge-triggered command bit to zero, also on timeout. */
    CH32H417_IP_mWriteReg(ip->BaseAddress, CH32H417_IP_CTRL1, 0U);
    return rc;
}

static int wait_bit_clear(const CH32H417_IP *ip, u32 reg, u32 mask)
{
    u32 remaining = ip->TimeoutUs;
    do {
        if (!(CH32H417_IP_mReadReg(ip->BaseAddress, reg) & mask))
            return CH32H417_IP_OK;
        usleep(1);
    } while (--remaining);
    return CH32H417_IP_TIMEOUT;
}

int CH32H417_IP_TimeCommand(CH32H417_IP *ip, u32 code, u32 source, u64 value)
{
    int rc;
    if (!valid_instance(ip) || code > 3U || source > 2U)
        return CH32H417_IP_INVALID;
    if (!(CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_STATUS0) & CH32H417_IP_TIME_CAPABLE))
        return CH32H417_IP_INVALID;
    rc = wait_bit_clear(ip, CH32H417_IP_TIME_CTRL, 1U << 31);
    if (rc != CH32H417_IP_OK) return rc;
    CH32H417_IP_mWriteReg(ip->BaseAddress, CH32H417_IP_TIME_SET_LO, (u32)value);
    CH32H417_IP_mWriteReg(ip->BaseAddress, CH32H417_IP_TIME_SET_HI, (u32)(value >> 32));
    CH32H417_IP_mWriteReg(ip->BaseAddress, CH32H417_IP_TIME_CTRL, code | (source << 4));
    (void)CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_TIME_CTRL);
    usleep(1);
    rc = wait_bit_clear(ip, CH32H417_IP_TIME_CTRL, 1U << 31);
    /* Let the periodic time snapshot pick up the new state. */
    usleep(5);
    return rc;
}

int CH32H417_IP_GetTime(const CH32H417_IP *ip, CH32H417_IP_Time *t)
{
    u32 lo, hi;
    if (!valid_instance(ip) || t == 0)
        return CH32H417_IP_INVALID;
    t->Flags = CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_TIME_STATUS);
    lo = CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_TIME_NOW_LO);  /* latches HI */
    hi = CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_TIME_NOW_HI);
    t->Now = ((u64)hi << 32) | lo;
    lo = CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_TIME_PPS_LO);
    hi = CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_TIME_PPS_HI);
    t->LastPps = ((u64)hi << 32) | lo;
    t->PpsCount = CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_PPS_COUNT);
    return CH32H417_IP_OK;
}

int CH32H417_IP_RxCommand(CH32H417_IP *ip, u32 flags, u64 time, u32 nsamp)
{
    int rc;
    if (!valid_instance(ip) || (flags & ~3U))
        return CH32H417_IP_INVALID;
    rc = wait_bit_clear(ip, CH32H417_IP_RX_CMD, 1U << 31);
    if (rc != CH32H417_IP_OK) return rc;
    CH32H417_IP_mWriteReg(ip->BaseAddress, CH32H417_IP_RX_CMD_TIME_LO, (u32)time);
    CH32H417_IP_mWriteReg(ip->BaseAddress, CH32H417_IP_RX_CMD_TIME_HI, (u32)(time >> 32));
    CH32H417_IP_mWriteReg(ip->BaseAddress, CH32H417_IP_RX_CMD_NSAMP, nsamp);
    CH32H417_IP_mWriteReg(ip->BaseAddress, CH32H417_IP_RX_CMD, flags);
    (void)CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_RX_CMD);
    usleep(1);
    return wait_bit_clear(ip, CH32H417_IP_RX_CMD, 1U << 31);
}

int CH32H417_IP_PopEvent(CH32H417_IP *ip, CH32H417_IP_Event *e)
{
    u32 info, lo, hi;
    if (!valid_instance(ip) || e == 0)
        return CH32H417_IP_INVALID;
    if (!(CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_EVT_STATUS) & 1U))
        return 0;
    info = CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_EVT_INFO);
    lo = CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_EVT_TIME_LO);
    hi = CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_EVT_TIME_HI);
    CH32H417_IP_mWriteReg(ip->BaseAddress, CH32H417_IP_EVT_STATUS, 1U);
    (void)CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_EVT_STATUS);
    e->Code = info >> 24;
    e->Lost = (info >> 23) & 1U;
    e->Seq = info & 0xFFFFU;
    e->Time = ((u64)hi << 32) | lo;
    return 1;
}

u32 CH32H417_IP_Version(const CH32H417_IP *ip)
{
    if (!valid_instance(ip)) return 0;
    if (!(CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_STATUS0) & CH32H417_IP_TIME_CAPABLE))
        return 0x01010000U;  /* pre-1.2 builds have no version register */
    return CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_VERSION);
}
