#include "battery_utils.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#if CONFIG_BATTERY_METRICS_ENABLE
#include "driver/adc.h"
#include "esp_adc_cal.h"
#endif

#define DEFAULT_VREF_S2    1100
#define ADC_ATTENUATION ADC_ATTEN_DB_11
#define ADC_RESOLUTION ADC_WIDTH_BIT_12

static const char *TAG = "battery_utils";

#if CONFIG_BATTERY_METRICS_ENABLE
static esp_adc_cal_characteristics_t *adc_chars = NULL;
static bool g_adc_initialized = false;
#endif

static bool g_gpio_sense_initialized = false;

float get_battery_voltage() {
#if CONFIG_BATTERY_METRICS_ENABLE
    // For ESP32-S2, ADC1 channels are 0-9. ADC1_CHANNEL_MAX is typically 10.
    // So valid channels are CONFIG_BATTERY_ADC_CHANNEL < ADC1_CHANNEL_MAX
    if (CONFIG_BATTERY_ADC_CHANNEL < 0 || CONFIG_BATTERY_ADC_CHANNEL >= ADC1_CHANNEL_MAX ) {
        ESP_LOGE(TAG, "Battery ADC channel not configured or invalid (%d). Max channel index is %d.", CONFIG_BATTERY_ADC_CHANNEL, ADC1_CHANNEL_MAX - 1);
        return -1.0f;
    }

    if (!g_adc_initialized) {
        ESP_LOGI(TAG, "Initializing ADC1 for battery voltage measurement on channel %d", CONFIG_BATTERY_ADC_CHANNEL);
        adc1_config_width(ADC_RESOLUTION);
        adc1_config_channel_atten((adc1_channel_t)CONFIG_BATTERY_ADC_CHANNEL, ADC_ATTENUATION);

        adc_chars = (esp_adc_cal_characteristics_t*)calloc(1, sizeof(esp_adc_cal_characteristics_t));
        esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTENUATION, ADC_RESOLUTION, DEFAULT_VREF_S2, adc_chars);

        if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
            ESP_LOGI(TAG, "ADC characterized using eFuse Vref: %u mV", adc_chars->vref);
        } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
            ESP_LOGI(TAG, "ADC characterized using Two Point eFuse values");
        } else {
            ESP_LOGI(TAG, "ADC characterized using default Vref: %u mV (DEFAULT_VREF_S2)", DEFAULT_VREF_S2);
        }
        g_adc_initialized = true;
    }

    int raw_adc = adc1_get_raw((adc1_channel_t)CONFIG_BATTERY_ADC_CHANNEL); // adc1_get_raw returns int
    if (raw_adc == -1) {
        ESP_LOGE(TAG, "Failed to read ADC channel %d", CONFIG_BATTERY_ADC_CHANNEL);
        return -1.0f;
    }

    uint32_t voltage_mv_at_pin = esp_adc_cal_raw_to_voltage((uint32_t)raw_adc, adc_chars);
    ESP_LOGD(TAG, "Raw ADC: %d, Calibrated Voltage at Pin: %lu mV", raw_adc, voltage_mv_at_pin);

    return (float)voltage_mv_at_pin / 1000.0f; // Return voltage at ADC pin in Volts

#else
    return -1.0f;
#endif
}

bool is_battery_present() {
#if CONFIG_BATTERY_METRICS_ENABLE
    if (CONFIG_BATTERY_SENSE_GPIO < 0) {
        return false;
    }

    if (!g_gpio_sense_initialized) {
        ESP_LOGI(TAG, "Initializing GPIO %d for battery presence sense", CONFIG_BATTERY_SENSE_GPIO);
        gpio_config_t io_conf;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << CONFIG_BATTERY_SENSE_GPIO);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);
        g_gpio_sense_initialized = true;
    }

    int level = gpio_get_level((gpio_num_t)CONFIG_BATTERY_SENSE_GPIO);
    ESP_LOGD(TAG, "Battery sense GPIO %d level: %d", CONFIG_BATTERY_SENSE_GPIO, level);
    return (level == 1);

#else
    return false;
#endif
}

uint8_t get_battery_charge_percentage(float voltage_at_pin, bool present) {
#if CONFIG_BATTERY_METRICS_ENABLE
    if (!present || voltage_at_pin < 0.0f) {
        return 0;
    }

    // Apply the voltage divider ratio from Kconfig
    float voltage_divider_ratio_float = (float)CONFIG_BATTERY_DIVIDER_RATIO_X100 / 100.0f;
    if (voltage_divider_ratio_float < 0.01f) { // Basic sanity check for the ratio
        voltage_divider_ratio_float = 1.0f; // Default to 1.0 if Kconfig is invalid
        ESP_LOGW(TAG, "CONFIG_BATTERY_DIVIDER_RATIO_X100 is invalid (%d), defaulting to 1.0x ratio.", CONFIG_BATTERY_DIVIDER_RATIO_X100);
    }
    float actual_battery_voltage = voltage_at_pin * voltage_divider_ratio_float;

    float min_v_actual = (float)CONFIG_BATTERY_MIN_VOLTAGE_MV / 1000.0f;
    float max_v_actual = (float)CONFIG_BATTERY_MAX_VOLTAGE_MV / 1000.0f;

    if (actual_battery_voltage < min_v_actual) {
        return 0;
    }
    if (actual_battery_voltage >= max_v_actual) {
        return 100;
    }
    if (max_v_actual <= min_v_actual) {
        return 0;
    }

    uint8_t percentage = (uint8_t)(((actual_battery_voltage - min_v_actual) / (max_v_actual - min_v_actual)) * 100.0f);

    ESP_LOGD(TAG, "Voltage at Pin: %.2fV, Actual Battery Voltage (est.): %.2fV, MinActualV: %.2fV, MaxActualV: %.2fV, Percentage: %d%%",
             voltage_at_pin, actual_battery_voltage, min_v_actual, max_v_actual, percentage);
    return percentage;

#else
    return 0;
#endif
}
