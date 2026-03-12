//
// Created by dan on 2026/3/3.
//

#ifndef BASICTIMER_BASICTIMER_H
#define BASICTIMER_BASICTIMER_H

#include <queue>
#include <mutex>
#include <thread>
#include <functional>
#include <condition_variable>

/*
 * BasicTimer calls callbacks in a dedicated thread sequentially to prevent the timing thread from getting stuck or lagging.
 * The potential "not yet called callbacks" will not be called if stop() being called.
 */

class BasicTimer {
    using CallBack = std::function<void()>;

    enum TimingThreadState {
        Starting,
        Started,

        Stopping,
        Stopped,
    };

public:
    BasicTimer();
    BasicTimer(BasicTimer&&) = delete;
    BasicTimer(const BasicTimer&) = delete;

    BasicTimer& operator=(BasicTimer&&) = delete;
    BasicTimer& operator=(const BasicTimer&) = delete;

    ~BasicTimer();

    void stop();
    void start();
    bool running();

    bool set_loop_times(std::uint32_t times);
    bool set_timeout(std::uint32_t milliseconds);

    // called at each timeout (excluding last timeout)
    bool set_timeout_callback(const CallBack &callback);
    // called at start of each timing loop
    bool set_timing_start_callback(const CallBack &callback);
    // called at the last timeout
    bool set_final_timeout_callback(const CallBack &callback);

private:
    void timing_thread();
    void callback_thread();

    void stop_internal();
    void join(std::thread &t);
    void async_call(const CallBack &callback);

    std::mutex m_api_mutex;
    std::mutex m_state_mutex;
    std::mutex m_callback_queue_mutex;

    std::thread m_timing_thread;
    std::thread m_callback_thread;

    TimingThreadState m_state{Stopped};
    bool m_destructing{false};
    std::queue<CallBack> m_callback_queue;

    std::condition_variable m_api_cv;
    std::condition_variable m_timing_thread_cv;
    std::condition_variable m_callback_thread_cv;

    std::uint32_t m_loop_times{0};
    std::uint32_t m_timeout_milliseconds{0};

    CallBack m_timeout_callback{nullptr};
    CallBack m_timing_start_callback{nullptr};
    CallBack m_final_timeout_callback{nullptr};
};

#endif //BASICTIMER_BASICTIMER_H
