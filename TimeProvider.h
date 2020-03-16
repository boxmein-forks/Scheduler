#pragma once

#include <chrono>

namespace Bosma {

class SystemClockTimeProvider {
public:
  using TimePointType = std::chrono::system_clock::time_point;
  using DurationType = std::chrono::system_clock::duration;
  using CompareType = std::less<std::chrono::system_clock::time_point>;

  static TimePointType now() {
    return std::chrono::system_clock::now();
  }

  static const time_t to_time_t(TimePointType tp) {
    return std::chrono::system_clock::to_time_t(tp);
  }

  static const struct tm to_struct_tm(TimePointType tp) {
    time_t timet = to_time_t(tp);
    return *std::localtime(&timet);
  }

  static const TimePointType from_time_t(time_t timet) {
    return std::chrono::system_clock::from_time_t(timet);
  }

  static const std::chrono::system_clock::time_point convertToTimePoint(TimePointType tp) {
    return tp;
  }
};

}
