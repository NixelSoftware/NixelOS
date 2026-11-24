#ifndef _ASSERT_H
#define _ASSERT_H

#ifdef NDEBUG
  #define assert(expr) ((void)0)
#else
  #include "../kernel/drivers/serial.h"

  #define _ASSERT_STR(x) #x
  #define _ASSERT_XSTR(x) _ASSERT_STR(x)

  #define assert(expr) \
      do { \
          if (!(expr)) { \
              debug_print("[ASSERT FAILED] " #expr " at " __FILE__ ":" _ASSERT_XSTR(__LINE__) "\r\n"); \
          } \
      } while (0)
#endif

#endif /* _ASSERT_H */