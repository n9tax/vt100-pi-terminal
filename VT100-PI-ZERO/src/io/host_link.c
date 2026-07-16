#include "io/host_link.h"

static host_write_fn active_write_fn = 0;

void host_link_set_write_fn(host_write_fn fn) { active_write_fn = fn; }

void host_write(const uint8_t *buf, uint32_t len) {
    if (active_write_fn) active_write_fn(buf, len);
}
