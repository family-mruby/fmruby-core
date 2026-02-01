#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SPI connection check task
 * @return 0 on success, -1 on error
 */
int spi_conn_check_init(void);

/**
 * @brief Start SPI connection check task
 * This creates a FreeRTOS task that periodically sends test data
 */
void spi_conn_check_start(void);

/**
 * @brief Stop SPI connection check task
 */
void spi_conn_check_stop(void);

#ifdef __cplusplus
}
#endif
