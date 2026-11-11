#ifndef NIXEL_SDK_EDITOR_H
#define NIXEL_SDK_EDITOR_H
#define SDK_SOURCE_MAX 16384
typedef struct {
    char text[SDK_SOURCE_MAX];
    int length,cursor,scroll,column_scroll,shift,caps,extended;
} sdk_editor;
void sdk_editor_set(sdk_editor*e,const char*source);
/* Accept raw PS/2 make/break bytes, as delivered by USER_WINDOW_RAW_KEYS. */
void sdk_editor_key(sdk_editor*e,unsigned scan);
void sdk_editor_follow(sdk_editor*e,int rows);
#endif
