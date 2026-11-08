#ifndef TEST_STUB_HARDWARE_SPI_H
#define TEST_STUB_HARDWARE_SPI_H
#include <stdint.h>
/* Host stubs: ui.cpp includes this for the hardware display path, which is
 * not exercised by the rendering tests. */
typedef struct spi_inst spi_inst_t;
static inline void spi_init(spi_inst_t *spi, uint32_t baud)
{
    (void)spi;
    (void)baud;
}
static inline void spi_set_baudrate(spi_inst_t *spi, uint32_t baud)
{
    (void)spi;
    (void)baud;
}
static inline void spi_write_blocking(spi_inst_t *spi, const uint8_t *src, size_t len)
{
    (void)spi;
    (void)src;
    (void)len;
}
#endif
