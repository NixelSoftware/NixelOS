/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NIXEL_IL3945_BSS_H
#define NIXEL_IL3945_BSS_H
#include "scan.h"
/* Transactional radio-IE parsing; unknown IEs are skipped after bounds checks.
 * Association responses must contain Supported Rates. Security is negotiated
 * separately and is never changed by this parser. */
int il3945_bss_parse(il_network_t*,const uint8_t*,unsigned,int require_rates);
unsigned il3945_bss_flags(const il_network_t*);
int il3945_bss_has_ofdm(const il_network_t*);
il_security_t il3945_rsn_security(const uint8_t*,unsigned);
#endif
