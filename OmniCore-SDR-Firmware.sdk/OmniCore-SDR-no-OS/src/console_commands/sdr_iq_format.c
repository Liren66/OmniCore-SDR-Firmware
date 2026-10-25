/* Explicit USB wire-format negotiation. AD9361 and the internal FPGA FIFO
 * retain signed 12-bit samples. IQ8 sends bits [11:4] in I1,Q1,I2,Q2 order;
 * TX reconstructs [11:0] as {wire_byte,4'b0000}. No MCU firmware change. */
#include "sdr_iq_format.h"
#include "sdr_runtime.h"
#include "console.h"

#define STREAM_BITS (CH32H417_IP_RX_EN | CH32H417_IP_TX_EN | CH32H417_IP_TX_RUN)
#define DUAL_BITS (CH32H417_IP_RX_DUAL | CH32H417_IP_TX_DUAL)

static CH32H417_IP *iq_bridge;
static unsigned selected_iq8;
static unsigned format_verified;

void sdr_iq_format_init(CH32H417_IP *device)
{
    /* Called only after the control layer has applied CTRL0=0 at startup. */
    iq_bridge = device;
    selected_iq8 = 0;
    format_verified = device != 0;
}

unsigned sdr_iq_format_bits(void)
{
    return selected_iq8 ? CH32H417_IP_IQ8 : 0;
}

unsigned sdr_iq_format_capable(void)
{
    CH32H417_IP_Status status;
    return iq_bridge && CH32H417_IP_GetStatus(iq_bridge, &status) == 0 &&
           !!(status.Status0 & CH32H417_IP_IQ8_CAPABLE);
}

static unsigned agrees(const CH32H417_IP_Status *status)
{
    unsigned expected = sdr_iq_format_bits();
    return format_verified &&
           !(status->Status0 & (CH32H417_IP_CFG_BUSY | CH32H417_IP_CFG_ERROR)) &&
           (status->RequestedConfig & CH32H417_IP_IQ8) == expected &&
           (status->AppliedConfig & CH32H417_IP_IQ8) == expected &&
           (!selected_iq8 || (status->Status0 & CH32H417_IP_IQ8_CAPABLE));
}

int sdr_iq_format_guard(unsigned rx_mask, unsigned tx_mask, unsigned tx_source)
{
    if (selected_iq8 && (rx_mask != 3 || tx_mask != 3 || tx_source != 1))
        return sdr_command_error("iq8_requires_2tx2rx_usb use=iq_format=12", -1);
    return 0;
}

int sdr_iq_format_check_start(void)
{
    CH32H417_IP_Status status;
    if (!iq_bridge || CH32H417_IP_GetStatus(iq_bridge, &status) || !agrees(&status))
        return sdr_command_error("iq_format_unverified stop_usb_and_reapply=iq_format", -1);
    return sdr_iq_format_guard(sdr_state.rx_mask, sdr_state.tx_mask, sdr_state.tx_source);
}

int sdr_iq_format_status(void)
{
    CH32H417_IP_Status status;
    unsigned applied, capable;
    if (!iq_bridge || CH32H417_IP_GetStatus(iq_bridge, &status))
        return sdr_command_error("bridge_unavailable", -1);
    applied = (status.AppliedConfig & CH32H417_IP_IQ8) ? 8 : 12;
    capable = !!(status.Status0 & CH32H417_IP_IQ8_CAPABLE);
    console_print(
        "iq_format=%u requested=%u applied=%u supported=%s cfg_busy=%u cfg_error=%u verified=%u scope=2tx2rx wire_group_bytes=%u sample_bytes=%u dual_wire_frame_bytes=%u dual_frames_per_group=2 fifo_group_bits=96 order=I1,Q1,I2,Q2\n",
        applied, (status.RequestedConfig & CH32H417_IP_IQ8) ? 8 : 12, applied,
        capable ? "12,8" : "12", !!(status.Status0 & CH32H417_IP_CFG_BUSY),
        !!(status.Status0 & CH32H417_IP_CFG_ERROR), agrees(&status),
        applied == 8 ? 8 : 12, applied == 8 ? 2 : 3, applied == 8 ? 4 : 6);
    return 0;
}

int sdr_iq_format_command(unsigned set, unsigned valid_bits)
{
    CH32H417_IP_Status status;
    unsigned config;
    int rc;
    if (!set)
        return sdr_iq_format_status();
    if (valid_bits != 8 && valid_bits != 12)
        return sdr_command_error("iq_format_requires_8_or_12", -1);
    if (!iq_bridge || !sdr_state.bridge_ready || CH32H417_IP_GetStatus(iq_bridge, &status))
        return sdr_command_error("bridge_unavailable", -1);
    if (sdr_state.usb_rx || sdr_state.usb_tx || sdr_state.tx_enabled ||
        ((status.RequestedConfig | status.AppliedConfig) & STREAM_BITS) ||
        (status.Status0 & CH32H417_IP_CFG_BUSY))
        return sdr_command_error("busy stop_tx_and_usb_before_iq_format_change", -1);
    if (valid_bits == 8) {
        if (!(status.Status0 & CH32H417_IP_IQ8_CAPABLE))
            return sdr_command_error("iq8_unsupported_fpga", -1);
        if (sdr_state.rx_mask != 3 || sdr_state.tx_mask != 3 || sdr_state.tx_source != 1)
            return sdr_command_error("iq8_requires_2tx2rx_usb", -1);
    }
    config = status.AppliedConfig & ~CH32H417_IP_IQ8;
    if (valid_bits == 8)
        config = (config & ~(CH32H417_IP_RX_CH1 | CH32H417_IP_TX_CH1)) |
                 DUAL_BITS | CH32H417_IP_IQ8;
    format_verified = 0;
    rc = CH32H417_IP_Configure(iq_bridge, config);
    if (rc) {
        /* No optimistic rollback: a timeout can leave an unknown CDC state.
         * Both directions were stopped before the attempt. A new successful
         * explicit format command is required before any USB start. */
        sdr_set_tx_gate(0);
        return sdr_command_error("iq_format applied=unknown inspect=iq_format?", rc);
    }
    selected_iq8 = valid_bits == 8;
    format_verified = 1;
    if (CH32H417_IP_GetStatus(iq_bridge, &status) || !agrees(&status)) {
        format_verified = 0;
        return sdr_command_error("iq_format readback_unverified inspect=iq_format?", -1);
    }
    return sdr_iq_format_status();
}
