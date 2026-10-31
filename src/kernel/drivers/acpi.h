#ifndef KERNEL_DRIVERS_ACPI_H
#define KERNEL_DRIVERS_ACPI_H

#include "../types.h"

int  acpi_init(void);
uint8_t acpi_rtc_century_register(void);
void acpi_power_off(void);
const char* acpi_power_error(void);
void acpi_reboot(void);

#endif