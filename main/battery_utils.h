#ifndef BATTERY_UTILS_H
#define BATTERY_UTILS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reads the battery voltage.
 *
 * This function will initialize and read the ADC channel specified by
 * CONFIG_BATTERY_ADC_CHANNEL.
 *
 * @return The battery voltage in volts. Returns 0.0f on error or if ADC not configured.
 */
float get_battery_voltage();

/**
 * @brief Checks if the battery is present.
 *
 * This function will initialize and read the GPIO pin specified by
 * CONFIG_BATTERY_SENSE_GPIO.
 *
 * @return True if the battery is detected as present, false otherwise.
 */
bool is_battery_present();

/**
 * @brief Calculates the battery charge percentage.
 *
 * Converts the given voltage to a percentage based on the min/max voltage
 * levels defined by CONFIG_BATTERY_MIN_VOLTAGE_MV and CONFIG_BATTERY_MAX_VOLTAGE_MV.
 *
 * @param voltage The current battery voltage in volts.
 * @param present Whether the battery is currently present.
 * @return Battery charge percentage (0-100). Returns 0 if not present or if voltage is outside valid range.
 */
uint8_t get_battery_charge_percentage(float voltage, bool present);

#ifdef __cplusplus
}
#endif

#endif // BATTERY_UTILS_H
