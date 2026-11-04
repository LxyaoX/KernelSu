#include <stdbool.h>
#include <stddef.h>

#include "uapi/supercall.h"

_Static_assert(sizeof(struct ksu_lookup_symbols_cmd) == 24, "lookup size");
_Static_assert(_Alignof(struct ksu_lookup_symbols_cmd) == 8,
               "lookup alignment");
_Static_assert(offsetof(struct ksu_lookup_symbols_cmd, names) == 0,
               "names offset");
_Static_assert(offsetof(struct ksu_lookup_symbols_cmd, addrs) == 8,
               "addrs offset");
_Static_assert(offsetof(struct ksu_lookup_symbols_cmd, count) == 16,
               "count offset");
_Static_assert(offsetof(struct ksu_lookup_symbols_cmd, flags) == 20,
               "flags offset");
_Static_assert(sizeof(struct ksu_get_info_cmd) == 16, "info size");
_Static_assert(sizeof(struct ksu_get_info_legacy_cmd) == 12,
               "legacy info size");

int main(void) {
  return KERNEL_SU_UAPI_VERSION != 6 ||
         KSU_GET_INFO_FLAG_PREINIT != (1U << 5) ||
         KSU_IOCTL_LOOKUP_SYMBOLS != 0x40184b16;
}
