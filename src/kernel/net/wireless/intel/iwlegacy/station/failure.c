/* SPDX-License-Identifier: GPL-2.0-only */
#include "private.h"
/* Snapshot before clear/reset, for both connection setup and live failures.
 * Do not infer a firmware crash from IL_VERIFY alone: host validation and
 * AP rejection share that result. IRQ/error records distinguish them. */
void il_station_capture_failure(il_station_t* s,int error) {
    s->failure_phase=s->phase;
    s->failure_security_phase=s->security.phase;
    s->failure_authorized=s->security.authorized;
    s->failure_active=s->active;
    s->last_data_tx=s->tx.submitted;
    s->runtime_error=error;
    s->runtime_irq=s->io.read32(s->io.context,CSR_INT);
    s->runtime_fh=s->io.read32(s->io.context,CSR_FH_INT_STATUS);
    if(error==IL_VERIFY)
        (void)il3945_read_error_log(&s->io,s->error_log_base,&s->firmware_error);
}
