        .c55x_large
        .ref  _c_int00
        .ref  _audio_dma_isr
        .def  _vector_table
        .sect ".vectors"

_vector_table:
        B     _c_int00
        NOP
        NOP
TRAP_ISR:
        B     TRAP_ISR
        NOP
        NOP
        .loop 6
        B     TRAP_ISR
        NOP
        NOP
        .endloop
        B     _audio_dma_isr
        NOP
        NOP
        .loop 23
        B     TRAP_ISR
        NOP
        NOP
        .endloop
        .end