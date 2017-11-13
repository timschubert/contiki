/*
 * Copyright  2008-2009 INRIA/SensTools
 *
 * <dev-team@sentools.info>
 *
 * This software is a set of libraries designed to develop applications
 * for the WSN430 embedded hardware platform.
 *
 * This software is governed by the CeCILL license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 * therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
 */
/**
 *addtogroup wsn430
 * @{
 */

/**
 *addtogroup spi1
 * @{
 */

/**
 *file
 *brief SPI1 driver
 *author Clément Burin des Roziers <clement.burin-des-roziers@inria.fr>
 *date October 09
 */

/**
 * @}
 */

/**
 * @}
 */

#include "gcc_uniarch/io.h"
#include "spi1-platform.h"

uint8_t spi1_write_single(uint8_t byte) {
    uint8_t dummy;
    U1TXBUF = byte;
    SPI_WAITFOREORx();
    dummy = U1RXBUF;

    return dummy;
}

uint8_t spi1_read_single(void) {
    return spi1_write_single(0x0);
}

uint8_t spi1_write(uint8_t* data, int16_t len) {
    uint8_t dummy=0;
    int16_t i;

    for (i=0; i<len; i++) {
        U1TXBUF = data[i];
        SPI_WAITFOREORx();
        dummy = U1RXBUF;
    }
    return dummy;
}
void spi1_read(uint8_t* data, int16_t len) {
    int16_t i;

    for (i=0; i<len; i++) {
        U1TXBUF = 0x0;
        SPI_WAITFOREORx();
        data[i] = U1RXBUF;
    }
}

void spi1_select(int16_t chip) {
    switch (chip) {
    case SPI1_CC1101:
        M25P80_SPI_DISABLE();
        DS1722_SPI_DISABLE();
        CC1101_SPI_ENABLE();
        break;
    case SPI1_DS1722:
        M25P80_SPI_DISABLE();
        CC1101_SPI_DISABLE();
        DS1722_SPI_DISABLE();
        U1CTL |= SWRST;
        U1TCTL &= ~(CKPH);
        U1CTL &= ~(SWRST);
        DS1722_SPI_ENABLE();
        break;
    case SPI1_M25P80:
        CC1101_SPI_DISABLE();
        DS1722_SPI_DISABLE();
        M25P80_SPI_ENABLE();
        break;
    default:
        break;
    }
}

void spi1_deselect(int16_t chip) {
    switch (chip) {
    case SPI1_CC1101:
        CC1101_SPI_DISABLE();
        break;
    case SPI1_DS1722:
        DS1722_SPI_DISABLE();
        U1CTL |= SWRST;
        U1TCTL |= CKPH;
        U1CTL &= ~(SWRST);
        break;
    case SPI1_M25P80:
        M25P80_SPI_DISABLE();
        break;
    default:
        break;
    }
}

int16_t spi1_read_somi(void) {
    return P5IN & (1<<2);
}
