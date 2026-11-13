#ifndef ANDROID_SHIM_ASSERT_H
#define ANDROID_SHIM_ASSERT_H
/* (the cores' asserts are left out, as with NDEBUG) */
#define assert(x) ((void)0)
#ifndef static_assert
#define static_assert _Static_assert
#endif
#endif
