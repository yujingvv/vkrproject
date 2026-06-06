-stack  0x0800
-heap   0x0400

MEMORY {
    VECS  (RX)  : origin = 0x000000, length = 0x000200
    DARAM (RWX) : origin = 0x000200, length = 0x00FE00
    SARAM (RWX) : origin = 0x010000, length = 0x03C000
}

SECTIONS {
    .vectors     > VECS
    .text        > SARAM
    .switch      > SARAM
    .cinit       > SARAM
    .const       > SARAM
    .cio         > SARAM
    .data        > SARAM
    .bss         > SARAM
    .far         > SARAM
    .stack       > DARAM
    .sysmem      > DARAM
    .saram       > SARAM
    .saram.const > SARAM
    .daram       > DARAM
    .ext         > SARAM
}