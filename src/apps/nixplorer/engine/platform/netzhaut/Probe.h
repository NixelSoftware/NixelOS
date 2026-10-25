#ifndef NH_NIXEL_PROBE_H
#define NH_NIXEL_PROBE_H
/* Integration check of original core/encoding; does not parse or render pages. */
int nh_nixel_probe(void);
int nh_nixel_probe_dom(void);
/* Partial HTML registry initialization; full trie/runtime is not yet ported. */
int nh_nixel_initialize_html(void);
int nh_nixel_initialize_css(void);
int nh_nixel_probe_html(void);
#endif
