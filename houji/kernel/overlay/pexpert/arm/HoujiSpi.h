/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef HOUJI_SPI_H
#define HOUJI_SPI_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {HoujiSpiOk=0,HoujiSpiNotReady=-1,HoujiSpiArgument=-2,HoujiSpiBusy=-3,
      HoujiSpiTimeout=-4,HoujiSpiReadback=-5};
int houji_spi_attach(uint32_t intid,int edge,void (*handler)(void *),void *context,
                     uint32_t *readback,uint64_t *router);
int houji_spi_dispatch(uint32_t intid);
uint64_t houji_spi_count(uint32_t intid);
#ifdef __cplusplus
}
#endif
#endif
