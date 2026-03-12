#include <iostream>
#include <unistd.h>

#include "BasicTimer.h"

BasicTimer timer;

void timing_start_callback() {
    static int count = 0;
    std::cout << "timing_start_callback(): " << ++count << std::endl;
    timer.start();
}

void timeout_callback() {
    static int count = 0;
    std::cout << "timeout_callback(): " << ++count << std::endl;
    timer.stop();
}

void final_timeout_callback() {
    static int count = 0;
    std::cout << "final_timeout_callback(): " << ++count << std::endl;
    timer.stop();
}

int main() {
    timer.set_loop_times(1000);
    timer.set_timeout(1);

    timer.set_timing_start_callback(timing_start_callback);
    timer.set_timeout_callback(timeout_callback);
    timer.set_final_timeout_callback(final_timeout_callback);


    for (auto i=0;i<100;i++) {
        std::cout << "timer start\n";
        timer.start();
        usleep(10000);

        std::cout << "timer stop\n";
        timer.stop();
        usleep(10000);
    }

    return 0;
}