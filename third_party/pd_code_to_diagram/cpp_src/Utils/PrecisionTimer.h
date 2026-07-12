#pragma once

#include <chrono>
#include <stdexcept>


class PrecisionTimer {
public:

    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using MsDuration = std::chrono::duration<double, std::milli>;

    PrecisionTimer() : is_running_(false), start_time_(), elapsed_ms_(0.0) {}


    void start() {
        if (is_running_) {
            throw std::runtime_error("Timer is already running!");
        }
        start_time_ = Clock::now();
        is_running_ = true;
    }


    void stop() {
        if (!is_running_) {
            throw std::runtime_error("Timer is not running!");
        }
        TimePoint end_time = Clock::now();
        elapsed_ms_ += MsDuration(end_time - start_time_).count();
        is_running_ = false;
    }


    void reset() {
        is_running_ = false;
        elapsed_ms_ = 0.0;
    }



    double get_elapsed_ms() const {
        if (is_running_) {
            TimePoint current_time = Clock::now();
            return elapsed_ms_ + MsDuration(current_time - start_time_).count();
        }
        return elapsed_ms_;
    }


    template <typename Func>
    static double measure(Func&& func) {
        PrecisionTimer timer;
        timer.start();
        std::forward<Func>(func)();
        timer.stop();
        return timer.get_elapsed_ms();
    }

private:
    int is_running_;
    TimePoint start_time_;
    double elapsed_ms_;
};
