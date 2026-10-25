#ifndef SDR_IQ_FORMAT_H
#define SDR_IQ_FORMAT_H

#include "CH32H417_IP.h"

/* Main-loop only. The default is packed signed 12-bit IQ. A format change
 * requires all USB requests to be quiesced by the host and RF TX stopped. */
void sdr_iq_format_init(CH32H417_IP *device);
unsigned sdr_iq_format_bits(void);
unsigned sdr_iq_format_capable(void);
int sdr_iq_format_guard(unsigned rx_mask, unsigned tx_mask, unsigned tx_source);
int sdr_iq_format_check_start(void);
int sdr_iq_format_command(unsigned set, unsigned valid_bits);
int sdr_iq_format_status(void);

#endif
