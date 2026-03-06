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
    m_request_all_threads_exit.store(true);

    m_timing_thread_cv.notify_one();
    m_callback_thread_cv.notify_one();

    if (m_timing_thread.joinable()) {
        //thread can not join itself
        if (std::this_thread::get_id() != m_timing_thread.get_id()) {
            m_timing_thread.join();
        } else {
            m_timing_thread.detach(); // last resort if self
        }
    }

    if (m_callback_thread.joinable()) {
        if (std::this_thread::get_id() != m_callback_thread.get_id()) {
            m_callback_thread.join();
        } else {
            m_callback_thread.detach(); // last resort if self
        }
    }
}

void BasicTimer::stop() {
    std::scoped_lock api_lk(m_api_mutex); {
        std::scoped_lock lk(m_timing_thread_mutex);

        if (!m_timing_thread_running.load()) {
            std::cout << "BasicTimer::stop(): warn: timer already stopped!\n";
            goto CLEAR_QUEUE;
        }

        m_timing_thread_request_stop.store(true);
    }
    m_timing_thread_cv.notify_one(); {
        std::unique_lock lk(m_timing_thread_mutex);
        m_timing_thread_cv.wait(lk, [this] { return !m_timing_thread_running.load(); });
    }

CLEAR_QUEUE: {
        std::scoped_lock lk(m_callback_thread_mutex);
        if (!m_callback_queue.empty()) {
            m_callback_queue = std::queue<CallBack>();
        }
    }
}

void BasicTimer::start() {
    std::scoped_lock api_lk(m_api_mutex); {
        if (m_loop_times.load() == 0 || m_timeout_milliseconds.load() == 0) {
            std::cout << "BasicTimer::start(): error: loop times or timeout(milliseconds) is invalid!\n";
            return;
        }

        std::scoped_lock lk(m_timing_thread_mutex);

        if (m_timing_thread_running.load()) {
            std::cout << "BasicTimer::start(): warn: timer already started!\n";
            return;
        }

        m_timing_thread_request_start.store(true);
    }
    m_timing_thread_cv.notify_one();

    std::unique_lock lk(m_timing_thread_mutex);
    m_timing_thread_cv.wait(lk, [this] { return m_timing_thread_running.load(); });
}

bool BasicTimer::running() {
    std::scoped_lock lk(m_api_mutex);

    return m_timing_thread_running.load();
}

bool BasicTimer::set_timeout(std::uint32_t milliseconds) {
    if (milliseconds == 0) {
        std::cout << "BasicTimer::set_timeout(): error: can't set timeout to 0 milliseconds!\n";
        return false;
    }

    std::scoped_lock lk(m_api_mutex, m_timing_thread_mutex);

    if (m_timing_thread_running.load()) {
        std::cout << "BasicTimer::set_timeout(): error: can't set timeout when timer is running!\n";
        return false;
    }

    m_timeout_milliseconds.store(milliseconds);
    return true;
}

bool BasicTimer::set_loop_times(std::uint32_t times) {
    if (times == 0) {
        std::cout << "BasicTimer::set_loop_times(): error: can't set loop times to 0!\n";
        return false;
    }

    std::scoped_lock lk(m_api_mutex, m_timing_thread_mutex);

    if (m_timing_thread_running.load()) {
        std::cout << "BasicTimer::set_loop_times(): error: can't set loop times when timer is running!\n";
        return false;
    }

    m_loop_times.store(times);
    return true;
}

bool BasicTimer::set_timeout_callback(const CallBack &callback) {
    std::scoped_lock lk(m_api_mutex, m_timing_thread_mutex);

    if (m_timing_thread_running.load()) {
        std::cout << "BasicTimer::set_timeout_callback(): error: can't set callback when timer is running!\n";
        return false;
    }

    m_timeout_callback = callback;
    return true;
}

bool BasicTimer::set_timing_start_callback(const CallBack &callback) {
    std::scoped_lock lk(m_api_mutex, m_timing_thread_mutex);

    if (m_timing_thread_running.load()) {
        std::cout << "BasicTimer::set_timing_start_callback(): error: can't set callback when timer is running!\n";
        return false;
    }

    m_timing_start_callback = callback;
    return true;
}

bool BasicTimer::set_final_timeout_callback(const CallBack &callback) {
    std::scoped_lock lk(m_api_mutex, m_timing_thread_mutex);

    if (m_timing_thread_running.load()) {
        std::cout << "BasicTimer::set_final_timeout_callback(): error: can't set callback when timer is running!\n";
        return false;
    }

    m_final_timeout_callback = callback;
    return true;
}

void BasicTimer::timing_thread() {
    while (true) {
        {
            std::scoped_lock lk(m_timing_thread_mutex);
            m_timing_thread_running.store(false);
        }
        m_timing_thread_cv.notify_one(); {
            std::unique_lock lk(m_timing_thread_mutex);
            m_timing_thread_cv.wait(lk, [this] {
                return m_timing_thread_request_start.load() || m_request_all_threads_exit.load();
            });

            if (m_request_all_threads_exit.load()) {
                return;
            }

            if (m_timing_thread_request_start.load()) {
                m_timing_thread_request_start.store(false);
            }

            m_timing_thread_running.store(true);
        }
        m_timing_thread_cv.notify_one();

        std::unique_lock lk(m_timing_thread_mutex);

        for (std::uint32_t i = 0; i < m_loop_times.load(); i++) {
            async_call_callback(m_timing_start_callback);

            m_timing_thread_cv.wait_for(
                lk, std::chrono::milliseconds(m_timeout_milliseconds.load()),
                [this] {
                    return m_timing_thread_request_stop.load() || m_request_all_threads_exit.load();
                });

            if (m_request_all_threads_exit.load()) {
                return;
            }

            if (m_timing_thread_request_stop.load()) {
                m_timing_thread_request_stop.store(false);
                break;
            }

            async_call_callback(m_timeout_callback);

            if (i == m_loop_times.load() - 1) {
                async_call_callback(m_final_timeout_callback);
            }
        }
    }
}

void BasicTimer::callback_thread() {
    while (true) {
        CallBack call_back; {
            std::unique_lock lk(m_callback_thread_mutex);

            if (m_callback_queue.empty()) {
                m_callback_thread_cv.wait(lk, [this] {
                    return m_request_all_threads_exit.load() || !m_callback_queue.empty();
                });
            }

            if (m_request_all_threads_exit.load()) {
                return;
            }

            call_back = m_callback_queue.front();
            m_callback_queue.pop();
        }

        try {
            call_back();
        } catch (...) {
            std::cout << "BasicTimer::callback_thread():error: callback throws an exception!" << std::endl;
        }
    }
}

void BasicTimer::async_call_callback(const CallBack &callback) {
    if (callback == nullptr) {
        return;
    } {
        std::scoped_lock lk(m_callback_thread_mutex);
        m_callback_queue.push(callback);
    }
    m_callback_thread_cv.notify_one();
}
