//
// Created by dan on 2026/3/3.
//

#include <mutex>
#include <thread>
#include <iostream>
#include <functional>
#include <condition_variable>

#include "BasicTimer.h"

BasicTimer::BasicTimer() {
    m_timing_thread = std::thread(&BasicTimer::timing_thread, this);
    m_callback_thread = std::thread(&BasicTimer::callback_thread, this);
}

BasicTimer::~BasicTimer() {
    std::scoped_lock lk(m_api_mutex);

    stop_internal();

    {
        std::scoped_lock llk(m_state_mutex, m_callback_queue_mutex);

        m_destructing = true;
        m_timing_thread_cv.notify_one();
        m_callback_thread_cv.notify_one();
    }

    join(m_timing_thread);
    join(m_callback_thread);
}

void BasicTimer::stop() {
    std::scoped_lock lk(m_api_mutex);

    stop_internal();
}

void BasicTimer::stop_internal() {
    std::unique_lock lk(m_state_mutex);

    if (m_state == Stopped) {
        std::cout <<  "already stopped!\n";
        goto END; // ensure callback queue is cleared
    }

    m_state = Stopping;
    m_timing_thread_cv.notify_one();

    m_api_cv.wait(lk, [this] { // internal unlocked
        return m_state == Stopped;
    });

END:
    std::scoped_lock llk(m_callback_queue_mutex);
    std::queue<CallBack>().swap(m_callback_queue);
}

void BasicTimer::start() {
    std::scoped_lock lk(m_api_mutex);
    std::unique_lock lk2(m_state_mutex);

    if (m_loop_times == 0 || m_timeout_milliseconds == 0) {
        std::cerr << "start failed: invalid loop times or timeout(milliseconds)!\n";
        return;
    }

    if (m_state == Started) {
        std::cout << "already running!\n";
        return;
    }

    m_state = Starting;
    m_timing_thread_cv.notify_one();

    m_api_cv.wait(lk2, [this] { // internal unlocked
        return m_state == Started;
    });
}

bool BasicTimer::running() {
    std::scoped_lock lk(m_api_mutex);
    std::scoped_lock lk2(m_state_mutex);

    return m_state == Started;
}

bool BasicTimer::set_timeout(std::uint32_t milliseconds) {
    std::scoped_lock lk(m_api_mutex);
    std::scoped_lock lk2(m_state_mutex);

    if (milliseconds == 0) {
        std::cerr << "can't set timeout to 0 milliseconds!\n";
        return false;
    }

    if (m_state == Started) {
        std::cerr << "can't set timeout when timer is running!\n";
        return false;
    }

    m_timeout_milliseconds = milliseconds;
    return true;
}

bool BasicTimer::set_loop_times(std::uint32_t times) {
    std::scoped_lock lk(m_api_mutex);
    std::scoped_lock lk2(m_state_mutex);

    if (times == 0) {
        std::cerr << "can't set loop times to 0!\n";
        return false;
    }

    if (m_state == Started) {
        std::cerr << "can't set loop times when timer is running!\n";;
        return false;
    }

    m_loop_times = times;
    return true;
}

bool BasicTimer::set_timeout_callback(const CallBack &callback) {
    std::scoped_lock lk(m_api_mutex);
    std::scoped_lock lk2(m_state_mutex);

    if (m_state == Started) {
        std::cerr << "can't set callback when timer is running!\n";;
        return false;
    }

    m_timeout_callback = callback;
    return true;
}

bool BasicTimer::set_timing_start_callback(const CallBack &callback) {
    std::scoped_lock lk(m_api_mutex);
    std::scoped_lock lk2(m_state_mutex);

    if (m_state == Started) {
        std::cerr << "can't set callback when timer is running!\n";;
        return false;
    }

    m_timing_start_callback = callback;
    return true;
}

bool BasicTimer::set_final_timeout_callback(const CallBack &callback) {
    std::scoped_lock lk(m_api_mutex);
    std::scoped_lock lk2(m_state_mutex);

    if (m_state == Started) {
        std::cerr << "can't set callback when timer is running!\n";
        return false;
    }

    m_final_timeout_callback = callback;
    return true;
}

void BasicTimer:: join(std::thread &t) {
    if (!t.joinable()) {
        std::cerr << "thread not joinable!!!\n";
        return;
    }

    if (std::this_thread::get_id() != t.get_id()) {
        t.join();
    } else {
        std::cerr << "thread can not join itself!!!\n";
        return;
    }
}

void BasicTimer::timing_thread() {
    std::unique_lock lk(m_state_mutex);

    while (true) {
        m_timing_thread_cv.wait(lk, [this] { // internal unlocked
            return m_state == Starting || m_destructing == true;
        });

        if (m_destructing == true) {
            return;
        }

        m_state = Started;
        m_api_cv.notify_one();

        for (std::uint32_t i = 0; i < m_loop_times; i++) {
            async_call(m_timing_start_callback);
            
            m_timing_thread_cv.wait_for( // internal unlocked
                lk, std::chrono::milliseconds(m_timeout_milliseconds),
                [this] {
                    return m_state == Stopping;
                });

            if (m_state == Stopping) {
                goto NOTIFY_STOPPED;
            }

            if (i == m_loop_times - 1) {
                async_call(m_final_timeout_callback);
            } else {
                async_call(m_timeout_callback);
            }
        }

        m_timing_thread_cv.wait( // internal unlocked
            lk,
            [this] {
                return m_state == Stopping;
            });

    NOTIFY_STOPPED:
        m_state = Stopped;
        m_api_cv.notify_one();
    }
}

void BasicTimer::callback_thread() {
    CallBack call_back;
    while (true) {
        {
            std::unique_lock lk(m_callback_queue_mutex);
            
            m_callback_thread_cv.wait(lk, [this] { // internal unlocked
                return !m_callback_queue.empty() || m_destructing == true;
            });

            if (m_destructing == true) {
                return;
            }

            call_back = m_callback_queue.front();
            m_callback_queue.pop();
        }

        // internal unlocked below
        try {
            if (call_back) {
                call_back();
            }
        } catch (...) {
            std::cerr << "callback throws an exception!\n";
        }
    }
}

void BasicTimer::async_call(const CallBack &callback) {
    std::scoped_lock lk(m_callback_queue_mutex);

    if (!callback) {
        return;
    }

    m_callback_queue.push(callback);
    m_callback_thread_cv.notify_one();
}
