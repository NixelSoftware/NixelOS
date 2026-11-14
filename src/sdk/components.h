#ifndef NIXEL_SDK_COMPONENTS_H
#define NIXEL_SDK_COMPONENTS_H
/* Public component objects. The designer supplies the named instances.
 * Event flags are read-only snapshots for the current event dispatch.
 * text and checkboxIsTicked may be assigned by application code. */
typedef struct {
    int _id, _kind;
    union { int buttonIsPressed; int isPressed; };
    union { int checkboxIsTicked; int isTicked; };
    union { int windowLoaded; int isLoaded; };
    int isOpen;
    char *text;
    int textChanged, isChanged;
} NixelComponent;
typedef NixelComponent Window;
typedef NixelComponent Button;
typedef NixelComponent TextBox;
typedef NixelComponent Label;
typedef NixelComponent Checkbox;
#define SDK_OBJECT_BYTES 36
#if defined(__i386__)
_Static_assert(sizeof(NixelComponent) == SDK_OBJECT_BYTES, "SDK component ABI");
#endif
#endif
