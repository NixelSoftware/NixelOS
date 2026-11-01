#ifndef USB_MASS_STORAGE_H
#define USB_MASS_STORAGE_H
#include "../../types.h"
#define USB_MSC_COUNT 2u
void usb_msc_probe(void);
void usb_msc_detach(uint8_t address);
int usb_msc_select(unsigned index);
int usb_msc_read(uint32_t lba, uint8_t count, uint8_t* buffer);
int usb_msc_write(uint32_t lba, uint8_t count, const uint8_t* buffer);
#endif
