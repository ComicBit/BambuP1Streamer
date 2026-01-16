#pragma once

#include <atomic>
#include <string>
#include <thread>

// Start a simple HTTP server in a background thread that responds to
// GET /stream_started with JSON {"started":true/false}.
// The server will run until `stop()` is called or the program exits.
class HttpServer {
public:
    HttpServer(int port, std::atomic<bool>* streamStartedFlag);
    ~HttpServer();

    // start listening in a background thread
    void start();
    // stop server and join thread
    void stop();

private:
    int port_;
    std::atomic<bool>* streamStarted_;
    int server_fd_;
    bool running_;
    std::thread thread_;
    void run();
};
