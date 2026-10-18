#include "floppy.h"


#define FDC_DOR      0x3F2 
#define FDC_MSR      0x3F4 
#define FDC_DATA     0x3F5 
#define FDC_CCR      0x3F7 


#define CMD_SPECIFY   0x03
#define CMD_WRITE     0xC5
#define CMD_READ      0xE6
#define CMD_RECAL     0x07
#define CMD_SENSE_INT 0x08
#define CMD_SEEK      0x0F


#define DMA_ADDR_CH2  0x04
#define DMA_COUNT_CH2 0x05
#define DMA_MASK_REG  0x0A
#define DMA_MODE_REG  0x0B
#define DMA_CLEAR_FF  0x0C
#define DMA_PAGE_CH2  0x81


#define DMA_BUFFER_ADDR 0x8000
static uint8_t* const dma_buffer = (uint8_t*)DMA_BUFFER_ADDR;

static uint8_t current_cylinder = 0;
static uint8_t motor_on = 0;

static void fdc_delay(int ticks) {
    for (volatile int i = 0; i < ticks * 5000; i++) {
        __asm__ volatile ("pause");
    }
}

static void dma_init(uint8_t mode, uint16_t length) {
    outb(DMA_MASK_REG, 0x06); 

    outb(DMA_CLEAR_FF, 0xFF); 
    outb(DMA_ADDR_CH2, DMA_BUFFER_ADDR & 0xFF);
    outb(DMA_ADDR_CH2, (DMA_BUFFER_ADDR >> 8) & 0xFF);
    outb(DMA_PAGE_CH2, (DMA_BUFFER_ADDR >> 16) & 0xFF);

    outb(DMA_CLEAR_FF, 0xFF);
    outb(DMA_COUNT_CH2, (length - 1) & 0xFF);
    outb(DMA_COUNT_CH2, ((length - 1) >> 8) & 0xFF);

    
    
    outb(DMA_MODE_REG, mode);

    outb(DMA_MASK_REG, 0x02); 
}

static int fdc_wait_ready(int for_read) {
    for (int i = 0; i < 50000; i++) {
        uint8_t msr = inb(FDC_MSR);
        if (for_read) {
            
            if ((msr & 0xC0) == 0xC0) return 1;
        } else {
            
            if ((msr & 0xC0) == 0x80) return 1;
        }
    }
    return 0;
}

static void fdc_write_byte(uint8_t byte) {
    if (fdc_wait_ready(0)) {
        outb(FDC_DATA, byte);
    }
}

static uint8_t fdc_read_byte(void) {
    if (fdc_wait_ready(1)) {
        return inb(FDC_DATA);
    }
    return 0;
}

static void fdc_motor_start(void) {
    if (!motor_on) {
        outb(FDC_DOR, 0x1C); 
        fdc_delay(300);      
        motor_on = 1;
    }
}

static void fdc_motor_stop(void) {
    if (motor_on) {
        outb(FDC_DOR, 0x0C); 
        motor_on = 0;
    }
}

static void fdc_sense_interrupt(uint8_t* st0, uint8_t* cyl) {
    fdc_write_byte(CMD_SENSE_INT);
    *st0 = fdc_read_byte();
    *cyl = fdc_read_byte();
}

static int fdc_recalibrate(void) {
    uint8_t st0, cyl;
    fdc_motor_start();

    for (int retry = 0; retry < 3; retry++) {
        fdc_write_byte(CMD_RECAL);
        fdc_write_byte(0x00); 

        fdc_delay(150);
        fdc_sense_interrupt(&st0, &cyl);

        if ((st0 & 0xC0) == 0x00 && cyl == 0) {
            current_cylinder = 0;
            return 1;
        }
    }
    return 0;
}

static int fdc_seek(uint8_t cyl, uint8_t head) {
    if (current_cylinder == cyl) return 1;

    uint8_t st0, current_cyl_read;
    fdc_write_byte(CMD_SEEK);
    fdc_write_byte((head << 2) | 0); 
    fdc_write_byte(cyl);

    fdc_delay(100);
    fdc_sense_interrupt(&st0, &current_cyl_read);

    if ((st0 & 0xC0) != 0x00 || current_cyl_read != cyl) {
        return 0;
    }
    current_cylinder = cyl;
    return 1;
}


static void lba_to_chs(uint32_t lba, uint8_t* cyl, uint8_t* head, uint8_t* sec) {
    *sec  = (uint8_t)((lba % 18) + 1);
    *head = (uint8_t)((lba / 18) % 2);
    *cyl  = (uint8_t)(lba / (18 * 2));
}

int floppy_init(void) {
    
    outb(FDC_DOR, 0x00);
    fdc_delay(10);
    outb(FDC_DOR, 0x0C); 

    
    outb(FDC_CCR, 0x00);

    
    uint8_t st0, cyl;
    for (int i = 0; i < 4; i++) {
        fdc_sense_interrupt(&st0, &cyl);
    }

    
    fdc_write_byte(CMD_SPECIFY);
    fdc_write_byte(0xDF); 
    fdc_write_byte(0x02); 

    if (!fdc_recalibrate()) {
        fdc_motor_stop();
        return 0;
    }

    fdc_motor_stop();
    return 1;
}

static int floppy_transfer_sector(uint32_t lba, uint8_t* buf, int is_write) {
    uint8_t cyl, head, sec;
    lba_to_chs(lba, &cyl, &head, &sec);

    fdc_motor_start();

    if (!fdc_seek(cyl, head)) {
        return 0;
    }

    if (is_write) {
        for (int i = 0; i < 512; i++) {
            dma_buffer[i] = buf[i];
        }
        dma_init(0x4A, 512); 
    } else {
        dma_init(0x46, 512); 
    }

    
    fdc_write_byte(is_write ? CMD_WRITE : CMD_READ);
    fdc_write_byte((head << 2) | 0); 
    fdc_write_byte(cyl);             
    fdc_write_byte(head);            
    fdc_write_byte(sec);             
    fdc_write_byte(0x02);            
    fdc_write_byte(18);              
    fdc_write_byte(0x1B);            
    fdc_write_byte(0xFF);            

    
    fdc_delay(60);

    
    uint8_t st0 = fdc_read_byte();
    uint8_t st1 = fdc_read_byte();
    uint8_t st2 = fdc_read_byte();
    (void)fdc_read_byte(); 
    (void)fdc_read_byte(); 
    (void)fdc_read_byte(); 
    (void)fdc_read_byte(); 

    
    if ((st0 & 0xC0) != 0x00 || st1 != 0x00 || st2 != 0x00) {
        return 0;
    }

    if (!is_write) {
        for (int i = 0; i < 512; i++) {
            buf[i] = dma_buffer[i];
        }
    }

    return 1;
}

int floppy_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    for (uint8_t s = 0; s < count; s++) {
        int ok = 0;
        for (int retry = 0; retry < 3; retry++) {
            if (floppy_transfer_sector(lba + s, buffer + (s * 512), 0)) {
                ok = 1;
                break;
            }
            fdc_recalibrate();
        }
        if (!ok) {
            fdc_motor_stop();
            return 0;
        }
    }
    fdc_motor_stop();
    return 1;
}

int floppy_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer) {
    for (uint8_t s = 0; s < count; s++) {
        int ok = 0;
        for (int retry = 0; retry < 3; retry++) {
            if (floppy_transfer_sector(lba + s, (uint8_t*)(buffer + (s * 512)), 1)) {
                ok = 1;
                break;
            }
            fdc_recalibrate();
        }
        if (!ok) {
            fdc_motor_stop();
            return 0;
        }
    }
    fdc_motor_stop();
    return 1;
}