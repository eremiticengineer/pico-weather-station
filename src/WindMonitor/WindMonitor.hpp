#pragma once

#include <cstdint>

#include "FreeRTOS.h"
#include "task.h"

// 1 revolution/sec = 1.492 mph
static constexpr int32_t WIND_SCALE_NUM = 14920;
static constexpr int32_t WIND_SCALE_DEN = 10000;

class WindMonitor {
public:
    // Running average of the last 10 x 1-second samples
    static constexpr int RUN_AVG_SIZE = 10;

    // Gust = rolling average over the last 3 seconds
    static constexpr int GUST_AVG_SIZE = 3;

    // Maximum gust for each of the last 60 minutes
    static constexpr int GUST_MIN_BUF = 60;

    WindMonitor()
        : total_clicks_(0),
          last_clicks_(0),
          run_sum_(0),
          run_idx_(0),
          run_filled_(0),
          gust_click_sum_(0),
          gust_idx_(0),
          gust_filled_(0),
          current_gust_(0),
          gust_minute_idx_(0) {

        for (int i = 0; i < RUN_AVG_SIZE; ++i) {
            run_buf_[i] = 0;
        }

        for (int i = 0; i < GUST_AVG_SIZE; ++i) {
            gust_click_buf_[i] = 0;
        }

        for (int i = 0; i < GUST_MIN_BUF; ++i) {
            gust_min_buf_[i] = 0;
        }
    }

    // ---------------------------------------------------------
    // GPIO ISR
    // ---------------------------------------------------------

    // Call from the wind sensor GPIO interrupt.
    // Keep this as short as possible.
    inline void onPulse() {
        total_clicks_++;
    }

    // ---------------------------------------------------------
    // 1-second sampling
    // ---------------------------------------------------------

    // Call once per second.
    //
    // Returns the wind speed for this 1-second interval,
    // expressed as mph * 10:
    //
    //     75 = 7.5 mph
    //
    // This also updates:
    //   - 10-second running average
    //   - rolling 3-second gust
    //   - current minute maximum gust
    inline int32_t sample1s() {
        uint32_t total = snapshotClicks_();

        // Unsigned subtraction deliberately handles uint32_t rollover.
        uint32_t clicks = total - last_clicks_;
        last_clicks_ = total;

        int32_t speed = toMphFromInterval10(clicks, 1000);

        updateRunningAverage_(speed);
        updateGust_(clicks);

        return speed;
    }

    // ---------------------------------------------------------
    // Minute rotation
    // ---------------------------------------------------------

    // Call once every 60 seconds.
    //
    // The current slot contains the maximum 3-second gust
    // observed during this minute.
    inline void rotateMinute() {
        gust_minute_idx_ = (gust_minute_idx_ + 1) % GUST_MIN_BUF;
        gust_min_buf_[gust_minute_idx_] = 0;
    }

    // ---------------------------------------------------------
    // Getters
    // ---------------------------------------------------------

    // Average of up to the last 10 x 1-second wind speeds.
    // Returns mph * 10.
    inline int32_t getRunningAverageMph() const {
        if (run_filled_ == 0) {
            return 0;
        }

        return static_cast<int32_t>(run_sum_ / run_filled_);
    }

    // Current rolling 3-second gust.
    // During the first 1-2 seconds after startup this uses
    // however many samples are available.
    //
    // Returns mph * 10.
    inline int32_t getCurrentGustMph() const {
        return current_gust_;
    }

    // Maximum gust recorded during the current minute.
    // Returns mph * 10.
    inline int32_t getCurrentMinuteMaxGustMph() const {
        return gust_min_buf_[gust_minute_idx_];
    }

    // Maximum gust recorded over the 60-minute buffer.
    // Returns mph * 10.
    inline int32_t getHourlyMaxGustMph() const {
        int32_t max_gust = 0;

        for (int i = 0; i < GUST_MIN_BUF; ++i) {
            if (gust_min_buf_[i] > max_gust) {
                max_gust = gust_min_buf_[i];
            }
        }

        return max_gust;
    }

    // ---------------------------------------------------------
    // Conversion
    // ---------------------------------------------------------

    // Convert pulse count over an interval to mph * 10.
    //
    // Examples:
    //
    //     75  = 7.5 mph
    //     149 = 14.9 mph
    //
    static inline int32_t toMphFromInterval10(
        uint32_t clicks,
        uint32_t interval_ms) {

        int64_t numerator =
            static_cast<int64_t>(WIND_SCALE_NUM) *
            clicks *
            1000LL *
            10LL;

        int64_t denominator =
            static_cast<int64_t>(WIND_SCALE_DEN) *
            interval_ms;

        return static_cast<int32_t>(
            (numerator + denominator / 2) / denominator);
    }

private:
    // ---------------------------------------------------------
    // Pulse counter
    // ---------------------------------------------------------

    inline uint32_t snapshotClicks_() const {
        taskENTER_CRITICAL();

        uint32_t value = total_clicks_;

        taskEXIT_CRITICAL();

        return value;
    }

    volatile uint32_t total_clicks_;
    uint32_t last_clicks_;

    // ---------------------------------------------------------
    // 10-second running average
    // ---------------------------------------------------------

    inline void updateRunningAverage_(int32_t speed) {
        run_sum_ -= run_buf_[run_idx_];

        run_buf_[run_idx_] = speed;
        run_sum_ += speed;

        run_idx_ = (run_idx_ + 1) % RUN_AVG_SIZE;

        if (run_filled_ < RUN_AVG_SIZE) {
            run_filled_++;
        }
    }

    int32_t run_buf_[RUN_AVG_SIZE];
    int64_t run_sum_;
    int run_idx_;
    int run_filled_;

    // ---------------------------------------------------------
    // Rolling 3-second gust
    // ---------------------------------------------------------

    inline void updateGust_(uint32_t clicks) {
        gust_click_sum_ -= gust_click_buf_[gust_idx_];

        gust_click_buf_[gust_idx_] = clicks;
        gust_click_sum_ += clicks;

        gust_idx_ = (gust_idx_ + 1) % GUST_AVG_SIZE;

        if (gust_filled_ < GUST_AVG_SIZE) {
            gust_filled_++;
        }

        uint32_t interval_ms =
            static_cast<uint32_t>(gust_filled_) * 1000;

        current_gust_ =
            toMphFromInterval10(gust_click_sum_, interval_ms);

        if (current_gust_ > gust_min_buf_[gust_minute_idx_]) {
            gust_min_buf_[gust_minute_idx_] = current_gust_;
        }
    }

    uint32_t gust_click_buf_[GUST_AVG_SIZE];
    uint32_t gust_click_sum_;
    int gust_idx_;
    int gust_filled_;

    int32_t current_gust_;

    // ---------------------------------------------------------
    // Maximum gust per minute
    // ---------------------------------------------------------

    int32_t gust_min_buf_[GUST_MIN_BUF];
    int gust_minute_idx_;
};