/* Exercise real transport completion/error/reset code against fake MMIO/DMA.
 * No device access: unused PCI discovery code is discarded by the host linker. */
#include "../virtio_gpu.c"
extern int puts(const char*);
static uint8_t fake_common[64];
static uint16_t fake_notify;
static uint32_t fake_backing[800*600];
static int mode, calls;
int32_t screen_width=800,screen_height=600;
void debug_print(const char* text) { (void)text; }
void driver_delay_us(uint32_t us) {
    (void)us; calls++;
    if(mode==2) return; /* lost completion */
    response.header.type=mode==1 ? 0x1205 : GPU_OK;
    queue.used_ring[consumed%QUEUE_SIZE].id=mode==3 ? 7 : 0;
    queue.used_ring[consumed%QUEUE_SIZE].length=sizeof(gpu_header_t);
    queue.used_index=consumed+1;
}
#define CHECK(x) do { if(!(x)) { puts("FAIL: " #x); return 1; } } while(0)
int main(void) {
    common=fake_common; notify=&fake_notify; backing=fake_backing;
    active=virgl_feature=1;
    write16(24,QUEUE_SIZE);
    static gpu_header_t packet={0x207,1,0,1,0};
    CHECK(virtio_gpu_command(&packet,sizeof(packet))==1);
    CHECK(active && virgl_feature && consumed==1);
    mode=1;
    CHECK(!virtio_gpu_command(&packet,sizeof(packet)));
    CHECK(active && virgl_feature && consumed==2 && !recover_2d_pending);
    mode=2;
    CHECK(!virtio_gpu_command(&packet,sizeof(packet)));
    CHECK(!active && !virgl_feature && recover_2d_pending && common[20]==0);
    mode=0;
    virtio_gpu_present();
    CHECK(active && !virgl_feature && !recover_2d_pending);
    CHECK(resource==1 && !dirty && screen_width==800 && screen_height==600);
    CHECK(consumed==5); /* create, attach, scanout, upload, flush */
    mode=3;
    CHECK(!virtio_gpu_command(&packet,sizeof(packet)));
    CHECK(!active && recover_2d_pending);
    /* If reset hasn't completed, don't reuse the outstanding DMA queue. */
    common[20]=1;
    unsigned before=consumed;
    mode=0;
    virtio_gpu_present();
    CHECK(!active && !recover_2d_pending && consumed==before);
    puts("PASS: command completion, optional rejection preserves display,");
    puts("      timeout/malformed completion, 2D recovery, reset DMA guard");
    return 0;
}
