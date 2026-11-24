#ifndef NIXEL_MAIL_H
#define NIXEL_MAIL_H
#define MAIL_MAX 131072
/* read/write return bytes, 0 on failure. */
typedef struct {
    void *context;
    int (*read)(void *, char *, unsigned);
    int (*write)(void *, const char *, unsigned);
    char buffer[4096]; unsigned pos, used;
} mail_io;
unsigned mail_length(const char *s);
void mail_copy(char *d, unsigned cap, const char *s);
int mail_equal(const char *a, const char *b);
int mail_safe(const char *s);
int mail_number(const char *s, unsigned *value);
int mail_line(mail_io *io, char *out, unsigned cap);
int mail_command(mail_io *io, const char *s);
int mail_imap_login(mail_io *io, const char *user, const char *pass);
int mail_imap_select(mail_io *io, unsigned *count);
int mail_imap_fetch(mail_io *io, unsigned number, char *out, unsigned cap);
int mail_pop_login(mail_io *io, const char *user, const char *pass);
int mail_pop_stat(mail_io *io, unsigned *count);
int mail_pop_fetch(mail_io *io, unsigned number, char *out, unsigned cap);
int mail_smtp(mail_io *io, const char *user, const char *pass,
              const char *from, const char *to, const char *subject, const char *body, long utc_seconds);
int mail_recipients(char *out, unsigned cap, const char *list, const char *exclude);
int mail_smtp_thread(mail_io *io, const char *user, const char *pass,
                     const char *from, const char *to, const char *subject,
                     const char *body, long utc_seconds, const char *reply_id);
void mail_header(const char *raw, const char *name, char *out, unsigned cap);
/* Extract preferred inline text/html or text/plain, decoding MIME transfer encoding. */
int mail_body(const char *raw, char *out, unsigned cap, int *html);
#endif
