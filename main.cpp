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

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <thread>

#include "date.h"
#include "zemo.hpp"

#define MAXLEN 128

// Add prefix for logging
std::string prefix() {
    return date::format("%F %T [PedlMonitor] ",
                        floor<std::chrono::milliseconds>(std::chrono::system_clock::now()));
}

// Catch CTRL-C and stop the monitor early
void endMonitoring(int signal) {
    stop_nvml_thread();
    std::cout << prefix() << "Caught signal " << signal << ", end monitoring." << std::endl;
    exit(signal);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " SLEEP_MS [GPU_IDX]" << std::endl
                  << "  Set DURATION to 0 to run indefinitely." << std::endl
                  << "  If SLEEP_MS is less than 10, it would be set to 10 instead" << std::endl;
        return 0;
    }

    int sleep_ms = std::atoi(argv[1]);
    int gpu_index = 0;
    if (argc > 2) { gpu_index = std::atoi(argv[2]); }
    std::cout << prefix() << "Monitor started.on GPU " << gpu_index << std::endl;
    if (sleep_ms < 10) {
        sleep_ms = 10;
    }
    std::cout << prefix() << "Sleeping " << sleep_ms << "ms after each poll. " << std::endl;

    signal(SIGINT, endMonitoring);

    start_nvml_thread(/* sleep_ms */ sleep_ms, /* gpu_index */ gpu_index);


    sockaddr_un server_un, client_un;
    socklen_t client_un_len;
    int listenfd, connfd, size;

    const char *home_dir = getenv("HOME");
    const char *socket_name = "monitor_server.socket";
    std::string _socket_path = home_dir + std::string("/") + socket_name + std::string(".") + std::to_string(gpu_index);
    const char *socket_path = _socket_path.c_str();

    if ((listenfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        perror("socket error");
        exit(1);
    }

    memset(&server_un, 0, sizeof(server_un));
    server_un.sun_family = AF_UNIX;
    strcpy(server_un.sun_path, socket_path);
    size = offsetof(struct sockaddr_un, sun_path) + strlen(server_un.sun_path);
    unlink(socket_path);
    if (bind(listenfd, (struct sockaddr *) &server_un, size) < 0) {
        perror("bind error");
        exit(1);
    }
    printf("UNIX domain socket bound\n");

    if (listen(listenfd, 1) < 0) {
        perror("listen() error");
        exit(1);
    }
    printf("Accepting connections...\n");

    char buf[MAXLEN];

    while (true) {
        client_un_len = sizeof(client_un);
        if ((connfd = accept(listenfd, (sockaddr *) &client_un, &client_un_len)) < 0) {
            perror("accept() error");
            continue;
        }

        printf("Client connected\n");

        while (true) {
            auto n = read(connfd, buf, MAXLEN);
            if (n < 0) {
                perror("read() error");
                break;
            } else if (n == 0) {
                printf("Client disconnected\n");
                break;
            }

            if (n == 1) {
                uint64_t energy_result = read_energy();
                write(connfd, &energy_result, sizeof(double));
            } else {
                printf("WTH?\n");
                while (true) {}
            }
        }

        close(connfd);
    }
}
