#ifdef TMS320_C5515

#include "aic3204.h"
#include <csl_i2c.h>
#include <csl_types.h>

static int aic_write(Uint8 reg, Uint8 val) {
    Uint16 buf[2] = { reg, val };
    return (CSL_I2C_write(AIC3204_I2C_ADDR, buf, 2u, 1u) == CSL_SOK) ? 0 : -1;
}
static int aic_page(Uint8 p) { return aic_write(0x00, p); }

int aic3204_init(void) {
    if (aic_page(0))           return -1;
    if (aic_write(0x01, 0x01)) return -1;  /* SW reset */
    { volatile int d=10000; while(d--); }

    if (aic_write(0x04, 0x03)) return -1;
    if (aic_write(0x05, 0x91)) return -1;
    if (aic_write(0x06, 0x08)) return -1;
    if (aic_write(0x07, 0x07)) return -1;
    if (aic_write(0x08, 0x80)) return -1;

    if (aic_write(0x0B, 0x83)) return -1;
    if (aic_write(0x0C, 0x90)) return -1;
    if (aic_write(0x0D, 0x00)) return -1;
    if (aic_write(0x0E, 0x80)) return -1;

    if (aic_write(0x12, 0x83)) return -1;
    if (aic_write(0x13, 0x90)) return -1;
    if (aic_write(0x14, 0x80)) return -1;

    if (aic_write(0x1B, 0x00)) return -1;
    if (aic_write(0x1C, 0x00)) return -1;

    if (aic_write(0x3F, 0xD4)) return -1;
    if (aic_write(0x40, 0x00)) return -1;
    if (aic_write(0x41, 0x00)) return -1;
    if (aic_write(0x42, 0x00)) return -1;

    if (aic_write(0x51, 0xC0)) return -1;
    if (aic_write(0x52, 0x00)) return -1;

    if (aic_page(1))           return -1;
    if (aic_write(0x0C, 0x08)) return -1;
    if (aic_write(0x0D, 0x08)) return -1;
    if (aic_write(0x09, 0x04)) return -1;
    if (aic_write(0x0A, 0x04)) return -1;
    if (aic_write(0x0B, 0x00)) return -1;
    if (aic_write(0x33, 0x40)) return -1;
    if (aic_write(0x34, 0x00)) return -1;
    if (aic_write(0x3B, 0x00)) return -1;

    return aic_page(0);
}

#endif /* TMS320_C5515 */