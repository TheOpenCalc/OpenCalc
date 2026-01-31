#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spi_inst {
    int unused;
} spi_inst_t;

static spi_inst_t _spi0_instance;
static spi_inst_t *const spi0 = &_spi0_instance;

static inline void spi_init(spi_inst_t *spi, uint32_t baudrate) {
    (void)spi; (void)baudrate;
}

static inline int spi_write_blocking(spi_inst_t *spi, const uint8_t *src, size_t len) {
    (void)spi; (void)src; (void)len;
    return (int)len;
}

#ifdef __cplusplus
}
#endif
