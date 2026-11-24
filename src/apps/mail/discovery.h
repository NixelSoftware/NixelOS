#ifndef MAIL_DISCOVERY_H
#define MAIL_DISCOVERY_H
#define MAIL_DNS_RECORDS 8
#define MAIL_DNS_MX 15u
#define MAIL_DNS_SRV 33u
typedef struct { char target[254]; unsigned priority,port; } mail_dns_record;
/* Build a DNS-over-HTTPS GET path; parse only matching, complete replies.
 * Returns record count, or -1 for malformed/error replies. */
int mail_dns_path(const char *name,unsigned type,char *path,unsigned cap);
int mail_dns_records(const unsigned char *data,unsigned length,const char *name,
                     unsigned type,mail_dns_record records[MAIL_DNS_RECORDS]);
#endif
