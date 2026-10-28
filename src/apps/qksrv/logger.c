#include "logger.h"
#include "../../system/user/sys_api.h"

static const char* const month_names[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static int append_str(char* buf, int cap, int* pos, const char* s) {
    if (!s) s = "-";
    while (*s && *pos < cap - 1) {
        buf[(*pos)++] = *s++;
    }
    buf[*pos] = '\0';
    return *pos < cap - 1;
}

static void append_2d(char* buf, int* pos, uint32_t val) {
    buf[(*pos)++] = (char)('0' + ((val / 10) % 10));
    buf[(*pos)++] = (char)('0' + (val % 10));
}

static void append_uint(char* buf, int cap, int* pos, uint32_t val) {
    char temp[12];
    int count = 0;
    if (val == 0) {
        append_str(buf, cap, pos, "0");
        return;
    }
    while (val > 0) {
        temp[count++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (count > 0 && *pos < cap - 1) {
        buf[(*pos)++] = temp[--count];
    }
    buf[*pos] = '\0';
}

void logger_init(void) {
    int32_t size = sys_fs_read(LOG_FILE_PATH, 0, 0);
    if (size < 0) {
        static const char init_msg[] = "# QuarkServe Access Log\n";
        sys_fs_write(LOG_FILE_PATH, init_msg, sizeof(init_msg) - 1);
    }
}

void logger_log_combined(const char* client_ip,
                         const char* request_line,
                         int status_code,
                         uint32_t bytes_sent,
                         const char* referer,
                         const char* user_agent) {
    char entry[512];
    int p = 0;

    /* %h %l %u */
    append_str(entry, sizeof(entry), &p, client_ip ? client_ip : "-");
    append_str(entry, sizeof(entry), &p, " - - [");

    /* [%t] -> [dd/MMM/yyyy:HH:mm:ss +0000] */
    user_rtc_time_t t;
    if (sys_rtc_time(&t) == 0) {
        append_2d(entry, &p, t.day);
        entry[p++] = '/';
        int m_idx = (t.month >= 1 && t.month <= 12) ? (t.month - 1) : 0;
        append_str(entry, sizeof(entry), &p, month_names[m_idx]);
        entry[p++] = '/';
        append_uint(entry, sizeof(entry), &p, t.year);
        entry[p++] = ':';
        append_2d(entry, &p, t.hour);
        entry[p++] = ':';
        append_2d(entry, &p, t.minute);
        entry[p++] = ':';
        append_2d(entry, &p, t.second);
        append_str(entry, sizeof(entry), &p, " +0000] \"");
    } else {
        append_str(entry, sizeof(entry), &p, "01/Jan/2026:00:00:00 +0000] \"");
    }

    /* "%r" */
    append_str(entry, sizeof(entry), &p, request_line ? request_line : "-");
    append_str(entry, sizeof(entry), &p, "\" ");

    /* %>s */
    append_uint(entry, sizeof(entry), &p, (uint32_t)status_code);
    entry[p++] = ' ';

    /* %b */
    append_uint(entry, sizeof(entry), &p, bytes_sent);
    append_str(entry, sizeof(entry), &p, " \"");

    /* "%{Referer}i" */
    append_str(entry, sizeof(entry), &p, referer ? referer : "-");
    append_str(entry, sizeof(entry), &p, "\" \"");

    /* "%{User-Agent}i" */
    append_str(entry, sizeof(entry), &p, user_agent ? user_agent : "-");
    append_str(entry, sizeof(entry), &p, "\"\n");

    /* Append entry to /quark.log */
    int32_t current_size = sys_fs_read(LOG_FILE_PATH, 0, 0);
    if (current_size < 0) current_size = 0;

    static uint8_t log_buffer[16384];
    if (current_size > 0 && current_size < (int)(sizeof(log_buffer) - sizeof(entry))) {
        sys_fs_read(LOG_FILE_PATH, log_buffer, current_size);
        for (int i = 0; i < p; i++) {
            log_buffer[current_size + i] = (uint8_t)entry[i];
        }
        sys_fs_write(LOG_FILE_PATH, log_buffer, current_size + p);
    } else {
        sys_fs_write(LOG_FILE_PATH, entry, p);
    }
}