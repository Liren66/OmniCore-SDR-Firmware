

/***************************** Include Files *******************************/
#include "CH32H417_IP.h"
#include "sleep.h"

/************************** Function Definitions ***************************/

static u32 applied_config(u32 status)
{
    return ((status >> 24) & 3U) | ((status >> 18) & 0x3CU) |
           ((status >> 20) & CH32H417_IP_TX_RUN) |
           ((status >> 21) & CH32H417_IP_IQ8);
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
    s->AppliedConfig = applied_config(s->Status0);
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
    *config = applied_config(CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_STATUS0));
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
        status = CH32H417_IP_mReadReg(ip->BaseAddress, CH32H417_IP_STATUS0);
        if (!(status & CH32H417_IP_CFG_BUSY) && applied_config(status) == config &&
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
    rc = CH32H417_IP_GetConfig(ip, &current);
    if (rc != CH32H417_IP_OK) return rc;
    disabled = current;
    if ((current ^ config) & CH32H417_IP_IQ8)
        disabled &= ~(CH32H417_IP_RX_EN | CH32H417_IP_TX_EN | CH32H417_IP_TX_RUN);
    if ((current ^ config) & (CH32H417_IP_RX_DUAL | CH32H417_IP_RX_CH1))
        disabled &= ~CH32H417_IP_RX_EN;
    if ((current ^ config) & (CH32H417_IP_TX_DUAL | CH32H417_IP_TX_CH1))
        disabled &= ~(CH32H417_IP_TX_EN | CH32H417_IP_TX_RUN);
    if (disabled != current) {
        rc = write_config(ip, disabled);
        if (rc != CH32H417_IP_OK) return rc;
    }
    modes = (disabled & (CH32H417_IP_RX_EN | CH32H417_IP_TX_EN | CH32H417_IP_TX_RUN)) |
            (config & (CH32H417_IP_RX_DUAL | CH32H417_IP_RX_CH1 |
                       CH32H417_IP_TX_DUAL | CH32H417_IP_TX_CH1 | CH32H417_IP_IQ8));
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
