#include "i18n.h"
#include "sdkconfig.h"

// Sprachwahl zur Compile-Zeit: nur eine Spalte landet im Binary.
#if defined(CONFIG_APP_LANG_EN)
#  define PICK(de, en) en
#else
#  define PICK(de, en) de
#endif

const char *const app_strings[STR_COUNT] = {
#define X(id, de, en) [STR_##id] = PICK(de, en),
    APP_STRINGS(X)
#undef X
};
