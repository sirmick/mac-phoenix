// Meson build config override — includes autoconf config.h then undefines things we don't need
#include "config.h"
#undef HAVE_SLIRP
