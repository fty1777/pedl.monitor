#ifndef ENERGYPROF_H
#define ENERGYPROF_H

// Copyright (C) 2022 Jae-Won Chung <jwnchung@umich.edu>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <nvml.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <thread>

#include "date.h"

using namespace date;

// Error checking.
#define die(s)                                                                                 \
    do {                                                                                       \
        std::cout << "ERROR: " << s << " in " << __FILE__ << ':' << __LINE__ << "\nAborting.." \
                  << std::endl;                                                                \
        exit(-1);                                                                              \
    } while (0)


#define checkNVML(status)                                               \
    do {                                                                \
        std::stringstream _err;                                         \
        if (status != NVML_SUCCESS) {                                   \
            _err << "NVML failure (" << nvmlErrorString(status) << ')'; \
            die(_err.str());                                            \
        }                                                               \
    } while (0)

// NVML power monitoring
volatile bool nvml_thread_running = false;
volatile bool nvml_thread_stopped = true;
volatile uint64_t energy_accum = 0;
std::thread nvml_thread;

// Poll power/smfreq/...
// `time` is in millisecond
// `energy_accum` is in mW*ms
void nvmlPollPowerAndAccumulate(nvmlDevice_t &device, int time_ms) {
    unsigned int power;
    checkNVML(nvmlDeviceGetPowerUsage(device, &power));
    energy_accum += time_ms * power;
}

void nvml_poll_accum(int sleep_ms, int gpu_index) {
    // Initialize NVML and get the given gpu_index's handle
    nvmlDevice_t device;
    checkNVML(nvmlInit_v2());
    checkNVML(nvmlDeviceGetHandleByIndex_v2(gpu_index, &device));
    char name[256];
    unsigned int device_count;
    checkNVML(nvmlDeviceGetCount_v2(&device_count));
    checkNVML(nvmlDeviceGetName(device, name, 255));
    printf("[pedl.monitor] Device to be monitored: %s (idx: %d, total: %u)\n", name, gpu_index, device_count);

    printf("[Polling thread] Start running\n");
    nvml_thread_running = true;

    auto sleep_to = std::chrono::steady_clock::now();
    std::chrono::milliseconds ms{sleep_ms};
    while (!nvml_thread_stopped) {
	    sleep_to += ms;
	    nvmlPollPowerAndAccumulate(device, sleep_ms);
	    std::this_thread::sleep_until(sleep_to);
    }

    // Shutdown NVML.
    checkNVML(nvmlShutdown());

    printf("[Polling thread] Stop running\n");
    nvml_thread_running = false;
}


void start_nvml_thread(int sleep_ms, int gpu_index) {
	if (!nvml_thread_stopped) { return; }
	nvml_thread_stopped = false;
	nvml_thread = std::thread(nvml_poll_accum, sleep_ms, gpu_index);
	while (!nvml_thread_running) {}
	printf("[Main thread] Polling thread started\n");
}

void stop_nvml_thread() {
	if (nvml_thread_stopped) { return; }
	nvml_thread_stopped = true;
	nvml_thread.join();
	printf("[Main thread] Polling thread stopped\n");
}

uint64_t read_energy() { return energy_accum; }

#endif /* ENERGYPROF_H */
