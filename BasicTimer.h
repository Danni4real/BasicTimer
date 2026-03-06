//
// Created by dan on 2026/3/3.
//

#ifndef BASICTIMER_BASICTIMER_H
#define BASICTIMER_BASICTIMER_H

#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <condition_variable>

/*
 * BasicTimer call callbacks in a dedicated thread sequentially to prevent timing thread from stuck or lag
 * The potential uncalled callbacks(previous callbacks run too slow) will still be called even if timer stopped automatically(finished looping)
 * The potential uncalled callbacks will not be called if timer stopped manually(by stop() method)
 */

class BasicTimer {
    using CallBack=std::function<void()>;

public:
    BasicTimer();
    ~BasicTimer();

    // stop timer and drop all uncalled callbacks
    void stop();

    void start();

    // whether timer is running(not be stopped automatically or manually)
    bool running();

    // must be set when timer is not running(be stopped automatically or manually)
    bool set_timeout(std::uint32_t milliseconds);

    // must be set when timer is not running(be stopped automatically or manually)
    bool set_loop_times(std::uint32_t times);

    // be called at each timeout(including last timeout), must be set when timer is not running(be stopped automatically or manually)
    bool set_timeout_callback(const CallBack &callback);

    // be called at start of each timing loop, must be set when timer is not running(be stopped automatically or manually)
    bool set_timing_start_callback(const CallBack &callback);

    // be called at last timeout, must be set when timer is not running(be stopped automatically or manually)
    bool set_final_timeout_callback(const CallBack &callback);

private:
    void timing_thread();
    void callback_thread();

    // add callback to queue and return
    void async_call_callback(const CallBack &callback);

    std::mutex m_api_mutex;

    std::atomic_uint m_loop_times{0};
    std::atomic_uint m_timeout_milliseconds{0};
    std::atomic_bool m_request_all_threads_exit {false};

    CallBack  m_timeout_callback {nullptr};
    CallBack  m_timing_start_callback {nullptr};
    CallBack  m_final_timeout_callback {nullptr};

    std::queue <CallBack>   m_callback_queue;
    std::thread             m_callback_thread;
    std::condition_variable m_callback_thread_cv;
    std::mutex              m_callback_thread_mutex;

    std::thread             m_timing_thread;
    std::condition_variable m_timing_thread_cv;
    std::mutex              m_timing_thread_mutex;
    std::atomic_bool        m_timing_thread_running {false};
    std::atomic_bool        m_timing_thread_request_stop {false};
    std::atomic_bool        m_timing_thread_request_start {false};
};

#endif //BASICTIMER_BASICTIMER_H
