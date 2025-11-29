#pragma once
#include <chrono>
#include <thread>

// return a monotonically increasing time in seconds (double)
// using this for tick timing, latency and interpolation
inline double now_seconds() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(
        steady_clock::now().time_since_epoch()
    ).count();
}

// sleep for a fractional number of seconds (delay)
inline void sleep_for_seconds(double seconds){
    if(seconds<=0.0) return;
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}
