#ifndef PSRAM_INIT_H
#define PSRAM_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize PSRAM hardware (MPU + Winbond OPI) on supported SPIC buses.
 * Call once before accessing PSRAM memory regions.
 */
void psram_init(void);

#ifdef __cplusplus
}
#endif

#endif /* PSRAM_INIT_H */