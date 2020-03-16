#pragma once

#include <iomanip>
#include <map>
#include <iostream>

#include "ctpl_stl.h"

#include "InterruptableSleep.h"
#include "Cron.h"
#include "TimeProvider.h"

namespace Bosma {
    template<typename _ClockType = SystemClockTimeProvider>
    class Task {
    public:
        explicit Task(std::function<void()> &&f, bool recur = false, bool interval = false) :
                f(std::move(f)), recur(recur), interval(interval) {}

        virtual typename _ClockType::TimePointType get_new_time() const = 0;

        std::function<void()> f;

        bool recur;
        bool interval;
    };

    template<typename _ClockType = SystemClockTimeProvider>
    class InTask : public Task<_ClockType> {
    public:
        explicit InTask(std::function<void()> &&f) : Task<_ClockType>(std::move(f)) {}

        // dummy time_point because it's not used
        typename _ClockType::TimePointType get_new_time() const override { return _ClockType::TimePointType(_ClockType::DurationType(0)); }
    };

    template<typename _ClockType = SystemClockTimeProvider>
    class EveryTask : public Task<_ClockType> {
    public:
        EveryTask(typename _ClockType::DurationType time, std::function<void()> &&f, bool interval = false) :
                Task<_ClockType>(std::move(f), true, interval), time(time) {}

        typename _ClockType::TimePointType get_new_time() const override {
          return _ClockType::now() + time;
        };
        typename _ClockType::DurationType time;
    };

    template<typename _ClockType = SystemClockTimeProvider>
    class CronTask : public Task<_ClockType> {
    public:
        CronTask(const std::string &expression, std::function<void()> &&f) : Task<_ClockType>(std::move(f), true),
                                                                             cron(expression) {}

        typename _ClockType::TimePointType get_new_time() const override {
          auto next = cron.cron_to_next();
          std::cout << "CronTask: next time = " << next << std::endl;
          return next;
        };
        Cron<_ClockType> cron;
    };

    inline bool try_parse(std::tm &tm, const std::string &expression, const std::string &format) {
      std::stringstream ss(expression);
      return !(ss >> std::get_time(&tm, format.c_str())).fail();
    }

    template<typename _ClockType = SystemClockTimeProvider>
    class Scheduler {
    public:
        explicit Scheduler(unsigned int max_n_tasks = 4) : done(false), threads(max_n_tasks + 1) {
          threads.push([this](int) {
              while (!done) {
                if (tasks.empty()) {
                  sleeper.sleep();
                } else {
                  auto time_of_first_task = (*tasks.begin()).first;
                  sleeper.sleep_until(_ClockType::convertToTimePoint(time_of_first_task));
                }
                manage_tasks();
              }
          });
        }

        Scheduler(const Scheduler &) = delete;

        Scheduler(Scheduler &&) noexcept = delete;

        Scheduler &operator=(const Scheduler &) = delete;

        Scheduler &operator=(Scheduler &&) noexcept = delete;

        ~Scheduler() {
          done = true;
          sleeper.interrupt();
        }

        template<typename _Callable, typename... _Args>
        void in(const typename _ClockType::TimePointType time, _Callable &&f, _Args &&... args) {
          std::shared_ptr<Task<_ClockType>> t = std::make_shared<InTask>(
                  std::bind(std::forward<_Callable>(f), std::forward<_Args>(args)...));
          add_task(time, std::move(t));
        }

        template<typename _Callable, typename... _Args>
        void in(const typename _ClockType::DurationType time, _Callable &&f, _Args &&... args) {
          in(_ClockType::now() + time, std::forward<_Callable>(f), std::forward<_Args>(args)...);
        }

        template<typename _Callable, typename... _Args>
        void at(const std::string &time, _Callable &&f, _Args &&... args) {
          // get current time as a tm object
          std::tm caltime  = _ClockType::to_struct_tm(_ClockType::now());

          // our final time as a time_point
          typename _ClockType::time_point tp;

          if (try_parse(caltime, time, "%H:%M:%S")) {
            // convert tm back to time_t, then to a time_point and assign to final
            tp = _ClockType::from_time_t(std::mktime(&caltime));

            // if we've already passed this time, the user will mean next day, so add a day.
            if (_ClockType::now() >= tp)
              tp += std::chrono::hours(24);
          } else if (try_parse(caltime, time, "%Y-%m-%d %H:%M:%S")) {
            tp = _ClockType::from_time_t(std::mktime(&caltime));
          } else if (try_parse(caltime, time, "%Y/%m/%d %H:%M:%S")) {
            tp = _ClockType::from_time_t(std::mktime(&caltime));
          } else {
            // could not parse time
            throw std::runtime_error("Cannot parse time string: " + time);
          }

          in(tp, std::forward<_Callable>(f), std::forward<_Args>(args)...);
        }

        template<typename _Callable, typename... _Args>
        void every(const typename _ClockType::DurationType time, _Callable &&f, _Args &&... args) {
          std::shared_ptr<Task<_ClockType>> t = std::make_shared<EveryTask<_ClockType>>(time, std::bind(std::forward<_Callable>(f),
                                                                                std::forward<_Args>(args)...));
          auto next_time = t->get_new_time();
          add_task(next_time, std::move(t));
        }

// expression format:
// from https://en.wikipedia.org/wiki/Cron#Overview
//    ┌───────────── minute (0 - 59)
//    │ ┌───────────── hour (0 - 23)
//    │ │ ┌───────────── day of month (1 - 31)
//    │ │ │ ┌───────────── month (1 - 12)
//    │ │ │ │ ┌───────────── day of week (0 - 6) (Sunday to Saturday)
//    │ │ │ │ │
//    │ │ │ │ │
//    * * * * *
        template<typename _Callable, typename... _Args>
        void cron(const std::string &expression, _Callable &&f, _Args &&... args) {
          std::shared_ptr<Task<_ClockType>> t = std::make_shared<CronTask<_ClockType>>(expression, std::bind(std::forward<_Callable>(f),
                                                                                     std::forward<_Args>(args)...));
          auto next_time = t->get_new_time();
          add_task(next_time, std::move(t));
          }

        template<typename _Callable, typename... _Args>
        void interval(const typename _ClockType::DurationType time, _Callable &&f, _Args &&... args) {
          std::shared_ptr<Task<_ClockType>> t = std::make_shared<EveryTask<_ClockType>>(time, std::bind(std::forward<_Callable>(f),
                                                                                std::forward<_Args>(args)...), true);
          add_task(_ClockType::now(), std::move(t));
        }

    private:
        std::atomic<bool> done;

        Bosma::InterruptableSleep sleeper;

        std::multimap<
            typename _ClockType::TimePointType,
            std::shared_ptr<Task<_ClockType>>,
            typename _ClockType::CompareType
        > tasks;
        std::mutex lock;
        ctpl::thread_pool threads;

        void add_task(const typename _ClockType::TimePointType time, std::shared_ptr<Task<_ClockType>> t) {
          std::lock_guard<std::mutex> l(lock);
          tasks.emplace(time, std::move(t));
          sleeper.interrupt();
        }

        void manage_tasks() {
          std::lock_guard<std::mutex> l(lock);

          auto end_of_tasks_to_run = tasks.upper_bound(_ClockType::now());

          // if there are any tasks to be run and removed
          if (end_of_tasks_to_run != tasks.begin()) {
            // keep track of tasks that will be re-added
            decltype(tasks) recurred_tasks;

            // for all tasks that have been triggered
            for (auto i = tasks.begin(); i != end_of_tasks_to_run; ++i) {

              auto &task = (*i).second;

              if (task->interval) {
                // if it's an interval task, only add the task back after f() is completed
                threads.push([this, task](int) {
                    task->f();
                    // no risk of race-condition,
                    // add_task() will wait for manage_tasks() to release lock
                    add_task(task->get_new_time(), task);
                });
              } else {
                threads.push([task](int) {
                    task->f();
                });
                // calculate time of next run and add the new task to the tasks to be recurred
                if (task->recur)
                  recurred_tasks.emplace(task->get_new_time(), std::move(task));
              }
            }

            // remove the completed tasks
            tasks.erase(tasks.begin(), end_of_tasks_to_run);

            // re-add the tasks that are recurring
            for (auto &task : recurred_tasks)
              tasks.emplace(task.first, std::move(task.second));
          }
        }
    };
}
