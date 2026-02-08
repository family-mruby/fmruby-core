#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize I2C and scan for devices
 * @return 0 on success, -1 on error
 */
int i2c_conn_check_init(void);

/**
 * @brief Scan I2C bus and log found devices
 * @param bus_num I2C bus number (1 or 2)
 * @return Number of devices found, or -1 on error
 */
int i2c_conn_check_scan(int bus_num);

/**
 * @brief Cleanup I2C resources
 */
void i2c_conn_check_deinit(void);

#ifdef __cplusplus
}
#endif
