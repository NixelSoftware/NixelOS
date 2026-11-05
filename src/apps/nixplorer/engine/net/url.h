#ifndef NIXPLORER_URL_H
#define NIXPLORER_URL_H
/* Scheme-less addresses use HTTPS. Only default HTTP/HTTPS ports are supported. */
int browser_url_secure(const char *url);
int browser_url_parse(const char *, char *, int, char *, int);
int browser_url_resolve(const char *location, const char *current_url, char *, int);
#endif
