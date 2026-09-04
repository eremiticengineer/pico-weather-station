#pragma once

#include "pico/stdlib.h"
#include "hardware/spi.h"

#include "FreeRTOS.h"
#include "task.h"

class WindDirectionMonitor {

public:

    struct WindDirectionData {
        const char* name;
        float degrees;
    };

    WindDirectionMonitor(
        spi_inst_t* spi_inst,
        uint cs,
        uint clk,
        uint mosi,
        uint miso
    )
        : spi(spi_inst),
          cs_pin(cs),
          clk_pin(clk),
          mosi_pin(mosi),
          miso_pin(miso) {
    }

    void init() {

        spi_init(spi, 100000);

        spi_set_format(
            spi,
            8,
            SPI_CPOL_0,
            SPI_CPHA_0,
            SPI_MSB_FIRST
        );

        gpio_set_function(clk_pin, GPIO_FUNC_SPI);
        gpio_set_function(mosi_pin, GPIO_FUNC_SPI);
        gpio_set_function(miso_pin, GPIO_FUNC_SPI);

        gpio_init(cs_pin);
        gpio_set_dir(cs_pin, GPIO_OUT);
        gpio_put(cs_pin, 1);
    }

    WindDirectionData getWindDirection() {
        uint16_t adc = readWindDirectionMedianADC();

        WindDirection direction = getDirectionFromADCValue(adc);

        return {
            windDirectionToString(direction),
            windDirectionToDegrees(direction)
        };
    }

private:

    spi_inst_t* spi;

    uint cs_pin;
    uint clk_pin;
    uint mosi_pin;
    uint miso_pin;

    enum class WindDirection {
        N,
        NNE,
        NE,
        ENE,
        E,
        ESE,
        SE,
        SSE,
        S,
        SSW,
        SW,
        WSW,
        W,
        WNW,
        NW,
        NNW,
        Unknown
    };

    inline void csSelect() {
        asm volatile("nop \n nop \n nop");
        gpio_put(cs_pin, 0);
        asm volatile("nop \n nop \n nop");
    }

    inline void csDeselect() {
        asm volatile("nop \n nop \n nop");
        gpio_put(cs_pin, 1);
        asm volatile("nop \n nop \n nop");
    }

    static const char* windDirectionToString(WindDirection direction) {
        switch (direction) {
            case WindDirection::N:   return "N";
            case WindDirection::NNE: return "NNE";
            case WindDirection::NE:  return "NE";
            case WindDirection::ENE: return "ENE";
            case WindDirection::E:   return "E";
            case WindDirection::ESE: return "ESE";
            case WindDirection::SE:  return "SE";
            case WindDirection::SSE: return "SSE";
            case WindDirection::S:   return "S";
            case WindDirection::SSW: return "SSW";
            case WindDirection::SW:  return "SW";
            case WindDirection::WSW: return "WSW";
            case WindDirection::W:   return "W";
            case WindDirection::WNW: return "WNW";
            case WindDirection::NW:  return "NW";
            case WindDirection::NNW: return "NNW";
            default:                 return "Unknown";
        }
    }

    static float windDirectionToDegrees(WindDirection direction) {
        switch (direction) {
            case WindDirection::N:   return 0.0f;
            case WindDirection::NNE: return 22.5f;
            case WindDirection::NE:  return 45.0f;
            case WindDirection::ENE: return 67.5f;
            case WindDirection::E:   return 90.0f;
            case WindDirection::ESE: return 112.5f;
            case WindDirection::SE:  return 135.0f;
            case WindDirection::SSE: return 157.5f;
            case WindDirection::S:   return 180.0f;
            case WindDirection::SSW: return 202.5f;
            case WindDirection::SW:  return 225.0f;
            case WindDirection::WSW: return 247.5f;
            case WindDirection::W:   return 270.0f;
            case WindDirection::WNW: return 292.5f;
            case WindDirection::NW:  return 315.0f;
            case WindDirection::NNW: return 337.5f;
            default:                 return -1.0f;
        }
    }

    uint16_t readWindDirectionMedianADC() {
        constexpr int samples = 9;
        constexpr int channel = 0;

        uint16_t values[samples];

        uint8_t buffer[3] = {
            1,
            static_cast<uint8_t>((8 + channel) << 4),
            0
        };

        for (int i = 0; i < samples; ++i) {

            uint8_t returnData[3];

            csSelect();

            spi_write_read_blocking(
                spi,
                buffer,
                returnData,
                sizeof(buffer)
            );

            csDeselect();

            values[i] = ((returnData[1] & 0x03) << 8) | returnData[2];

            vTaskDelay(pdMS_TO_TICKS(2));
        }

        // Insertion sort - ideal for only 9 samples.
        for (int i = 1; i < samples; ++i) {

            uint16_t value = values[i];
            int j = i - 1;

            while (j >= 0 && values[j] > value) {
                values[j + 1] = values[j];
                --j;
            }

            values[j + 1] = value;
        }

        return values[samples / 2];
    }

    WindDirection getDirectionFromADCValue(uint16_t adc) {
        if (adc <= 83) {
            return WindDirection::ESE;
        }
        else if (adc <= 96) {
            return WindDirection::ENE;
        }
        else if (adc <= 117) {
            return WindDirection::E;
        }
        else if (adc <= 162) {
            return WindDirection::SSE;
        }
        else if (adc <= 220) {
            return WindDirection::SE;
        }
        else if (adc <= 271) {
            return WindDirection::SSW;
        }
        else if (adc <= 350) {
            return WindDirection::S;
        }
        else if (adc <= 436) {
            return WindDirection::NNE;
        }
        else if (adc <= 532) {
            return WindDirection::NE;
        }
        else if (adc <= 616) {
            return WindDirection::WSW;
        }
        else if (adc <= 667) {
            return WindDirection::SW;
        }
        else if (adc <= 744) {
            return WindDirection::NNW;
        }
        else if (adc <= 806) {
            return WindDirection::N;
        }
        else if (adc <= 857) {
            return WindDirection::WNW;
        }
        else if (adc <= 915) {
            return WindDirection::NW;
        }
        else {
            return WindDirection::W;
        }
    }
};