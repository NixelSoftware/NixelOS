#ifndef KERNEL_DRIVERS_ACPI_H
#define KERNEL_DRIVERS_ACPI_H

#include "../types.h"

int  acpi_init(void);
void acpi_power_off(void);

#endif