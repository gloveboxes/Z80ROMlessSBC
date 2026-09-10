#ifndef TEST_HARDWARE_SPI_H
#define TEST_HARDWARE_SPI_H
#include "pico/stdlib.h"
typedef int spi_inst_t;
extern spi_inst_t *spi0;
enum { SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST };
uint spi_init(spi_inst_t *spi, uint hz);
void spi_set_format(spi_inst_t *spi, uint bits, uint polarity, uint phase, uint order);
int spi_write_blocking(spi_inst_t *spi, const uint8_t *data, size_t count);
int spi_write_read_blocking(spi_inst_t *spi, const uint8_t *tx, uint8_t *rx, size_t count);
#endif