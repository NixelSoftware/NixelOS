#ifndef NIXEL_SDK_H
#define NIXEL_SDK_H
/* NCC's built-in declarations. Coordinates include the 26-pixel title bar.
 * Control IDs are positive; poll returns a clicked ID, 0 idle, -1 closed. */
int window(char *title, int width, int height);
int button(int id, int x, int y, int width, int height, char *text);
int textbox(int id, int x, int y, int width, int height, char *text);
int label(int id, int x, int y, int width, int height, char *text);
int checkbox(int id, int x, int y, int width, int height, char *text);
int poll(void);
int set_text(int id, char *text);
char *get_text(int id);
int checked(int id);
int close_window(void);
int print(char *text);
int ticks(void);
int yield(void);
#endif
