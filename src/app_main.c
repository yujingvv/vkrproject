#ifdef TMS320_C5515

#include "config.h"
#include "util/fixed_point.h"
#include "algo/pipeline.h"
#include "hal/bsl/aic3204.h"
#include "hal/bsl/audio_io.h"
#include <csl_i2c.h>
#include <csl_sysctrl.h>
#include <csl_types.h>

static void sysclk_init(void) {
    SYS_setEBSR(CSL_EBSR_FIELD_PPMODE,  CSL_EBSR_PPMODE_1);
    SYS_setEBSR(CSL_EBSR_FIELD_SP0MODE, CSL_EBSR_SP0MODE_1);
    CSL_PLL_config pllCfg = {
        .pllMult=12u, .pllDiv=0u, .pllIndiv=0u,
        .pllOutDiv=1u, .pllClkSrc=0u, .sysclkDiv=0u
    };
    (void)pllCfg;
}

static int i2c_init(void) {
    CSL_Status status = CSL_I2C_init(CSL_I2C_CLKRATE_100kHz);
    if (status != CSL_SOK) return -1;
    CSL_I2cConfig i2cCfg = {
        .icoar=AIC3204_I2C_ADDR, .icimr=0u,
        .icclkl=5u, .icclkh=5u,
        .icsar=AIC3204_I2C_ADDR, .icmdr=0x0620u,
        .icemdr=0u, .icpsc=29u
    };
    status = CSL_I2C_config(&i2cCfg);
    return (status == CSL_SOK) ? 0 : -1;
}

int main(void) {
    sysclk_init();
    pipeline_init();
    if (i2c_init()      != 0) for (;;);
    if (aic3204_init()  != 0) for (;;);
    if (audio_io_init() != 0) for (;;);
    audio_io_start();
    for (;;) { asm(" IDLE"); }
}

#endif /* TMS320_C5515 */