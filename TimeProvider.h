#pragma once

#include <chrono>

namespace Bosma {

class SystemClockTimeProvider {
public:
  using TimePointType = std::chrono::system_clock::time_point;
  using DurationType = std::chrono::system_clock::duration;

  static TimePointType now() {
    return std::chrono::system_clock::now();
  }

  static const time_t to_time_t(TimePointType tp) {
    return std::chrono::system_clock::to_time_t(tp);
  }

  static const TimePointType from_time_t(time_t timet) {
    return std::chrono::system_clock::from_time_t(timet);
  }
};

}