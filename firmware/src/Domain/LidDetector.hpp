#pragma once

#include <cstdint>
#include <algorithm>

namespace SmokerController::Domain {

struct LidDetectorConfig {
    float drop_threshold_deg{15.0f};   // Drop in degrees to trigger detection
    uint32_t time_window_ms{30000};    // 30 second evaluation window
    uint32_t pause_duration_ms{180000}; // 180 second airflow suppression window
};

class LidOpenDetector {
public:
    explicit LidOpenDetector(const LidDetectorConfig& config = {}) noexcept
        : config_(config),
          is_active_(false),
          triggered_at_ms_(0),
          head_(0),
          count_(0) {}

    [[nodiscard]] bool isActive() const noexcept { return is_active_; }

    void reset() noexcept {
        is_active_ = false;
        triggered_at_ms_ = 0;
        count_ = 0;
        head_ = 0;
    }

    void trigger(uint32_t current_time_ms) noexcept {
        is_active_ = true;
        triggered_at_ms_ = current_time_ms;
    }

    bool update(float current_temp, uint32_t current_time_ms) noexcept {
        // If active, check if pause duration has elapsed
        if (is_active_) {
            uint32_t elapsed = current_time_ms - triggered_at_ms_;
            if (elapsed >= config_.pause_duration_ms) {
                is_active_ = false;
                triggered_at_ms_ = 0;
                count_ = 0;
                head_ = 0;
            } else {
                return true;
            }
        }

        // Add sample to static ring buffer (capacity 64 samples)
        history_[head_] = Sample{current_time_ms, current_temp};
        head_ = (head_ + 1) % MAX_SAMPLES;
        if (count_ < MAX_SAMPLES) {
            count_++;
        }

        if (count_ < 2) {
            return false;
        }

        // Find maximum temperature within the rolling time window
        float max_temp_in_window = current_temp;
        for (size_t i = 0; i < count_; ++i) {
            const auto& sample = history_[i];
            if ((current_time_ms - sample.timestamp_ms) <= config_.time_window_ms) {
                if (sample.temp > max_temp_in_window) {
                    max_temp_in_window = sample.temp;
                }
            }
        }

        float drop = max_temp_in_window - current_temp;
        if (drop >= config_.drop_threshold_deg) {
            is_active_ = true;
            triggered_at_ms_ = current_time_ms;
            return true;
        }

        return false;
    }

private:
    static constexpr size_t MAX_SAMPLES = 64;
    struct Sample {
        uint32_t timestamp_ms{0};
        float temp{0.0f};
    };

    LidDetectorConfig config_;
    bool is_active_;
    uint32_t triggered_at_ms_;
    Sample history_[MAX_SAMPLES]{};
    size_t head_;
    size_t count_;
};

} // namespace SmokerController::Domain
