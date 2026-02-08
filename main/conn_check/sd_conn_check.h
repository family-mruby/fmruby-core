#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SD card interface and check connection
 * @return 0 on success, -1 on error
 */
int sd_conn_check_init(void);

/**
 * @brief Test SD card read/write operations
 * @return 0 on success, -1 on error
 */
int sd_conn_check_test(void);

/**
 * @brief Cleanup SD card connection check resources
 */
void sd_conn_check_deinit(void);

#ifdef __cplusplus
}
#endif
