#ifndef XTENSA_CPU_ESP32S3_H
#define XTENSA_CPU_ESP32S3_H
#include <stdint.h>

typedef union {
    int8_t s8[16] QEMU_ALIGNED(16);
    uint8_t u8[16];
    int16_t s16[8];
    uint16_t u16[8];
    uint32_t u32[4];
    int32_t  s32[4];
    uint64_t u64[2];
} Q_reg;

typedef Q_reg esp_qreg_t;

typedef union {
    uint8_t u8[20];
} ACCQ_reg;

typedef struct CPUXtensaEsp32s3State_s {
    Q_reg Q[8]; /* Active Q registers. */
    ACCQ_reg ACCQ[2];
    Q_reg  UA_STATE;
    int64_t ACCX;
    uint8_t SAR_BYTE;
    uint8_t fft_width;
    uint8_t gpio_out;
    /* Temporary register for EE.FFT.AMS.S16.LD.INCP.UAUP. */
    Q_reg temp;
    /* Temporary register value for EE.FFT.AMS.S16.ST.INCP. */
    int16_t temp_asm[2];
} CPUXtensaEsp32s3State;
#endif /* XTENSA_CPU_ESP32S3_H */
