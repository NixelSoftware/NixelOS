#ifndef MAIL_RENDER_H
#define MAIL_RENDER_H
/* Safe native HTML subset. No resource loading, navigation, scripts or forms. */
void mail_render_html(const char *html, void (*emit)(const char *, int accent));
#endif
