#ifndef TZ_CONFIG_H
#define TZ_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void tz_config_init(void);

void tz_config_lock_sau(void);

void tz_config_lock_debug(void);

void tz_config_lock_all(void);

bool tz_config_is_sau_locked(void);

#ifdef __cplusplus
}
#endif

#endif
