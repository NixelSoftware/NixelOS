/* Storage adapter for the complete original Unicode table. Returned records
 * remain valid for the allocator lifetime, matching the original table API. */
#include "Runtime.h"
#include "../../lib/nh-encoding/Base/UnicodeData.gen.h"
#include "../../lib/nh-core/System/Memory.h"
#include <string.h>
#ifdef NH_NIXEL_HOST_TEST
#include <fcntl.h>
#include <unistd.h>
#else
#include "../nixel.h"
#endif

static char *records[NH_ENCODING_UNICODE_DATA_COUNT];
void nh_nixel_unicode_reset(void) { memset(records, 0, sizeof(records)); }
const char *nh_nixel_unicode_record(unsigned index) {
    if (index >= NH_ENCODING_UNICODE_DATA_COUNT) return 0;
    if (records[index]) return records[index];
    char *record = nh_core_allocate(255);
    if (!record) return 0;
    int count;
#ifdef NH_NIXEL_HOST_TEST
    int file = open("UNICODE.DAT", O_RDONLY);
    if (file < 0) {nh_core_free(record); return 0;}
    count = (int)pread(file, record, 255, (off_t)index * 255);
    close(file);
#else
    count = sys_fs_read_at("UNICODE.DAT", record, 255, index * 255);
#endif
    if (count != 255 || record[254] != 0) {
        nh_core_free(record);
        nh_core_debug("Netzhaut: missing or invalid UNICODE.DAT record");
        return 0;
    }
    records[index] = record;
    return record;
}
