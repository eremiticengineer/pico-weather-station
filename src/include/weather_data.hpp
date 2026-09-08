# pragma once

#include <cstdint>

// Startup for sensors to report readiness
constexpr EventBits_t BME280_READY          = 1 << 0;
constexpr EventBits_t VEML7700_READY        = 1 << 1;
constexpr EventBits_t WIND_DIRECTION_READY  = 1 << 2;
constexpr EventBits_t WIND_SPEED_READY      = 1 << 3;
constexpr EventBits_t DS3231_READY          = 1 << 4;
constexpr EventBits_t ALL_READY =
    BME280_READY |
    VEML7700_READY |
    WIND_DIRECTION_READY |
    WIND_SPEED_READY |
    DS3231_READY;

constexpr uint8_t SENSOR_VALID_BME280         = 1u << 0;
constexpr uint8_t SENSOR_VALID_VEML7700       = 1u << 1;
constexpr uint8_t SENSOR_VALID_WIND_DIRECTION = 1u << 2;
constexpr uint8_t SENSOR_VALID_RAIN_COUNT     = 1u << 3;
constexpr uint8_t SENSOR_VALID_WIND_SPEED     = 1u << 4;
constexpr uint8_t SENSOR_VALID_DS3231         = 1u << 5;

constexpr uint8_t ALL_SENSORS_VALID =
    SENSOR_VALID_BME280 |
    SENSOR_VALID_VEML7700 |
    SENSOR_VALID_RAIN_COUNT |
    SENSOR_VALID_WIND_SPEED |
    SENSOR_VALID_WIND_DIRECTION |
    SENSOR_VALID_DS3231;

struct WeatherData {
    float temperature = 0.0f;
    float humidity = 0.0f;
    float pressure = 0.0f;

    float windSpeed = 0.0f;
    float windGust = 0.0f;
    char windDirectionName[4];
    uint16_t windDirectionDegrees = 0;

    float lux = 0.0f;

    uint32_t rainTipsSinceBoot = 0;
    uint32_t bootId = 0;

    float batteryVoltage = 0.0f;

    uint32_t timestamp = 0;

    char dateTime[20] = "";

    uint8_t validSensors;
};
