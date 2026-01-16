#include "HttpServer.h"

#include <atomic>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <sstream>
#include <vector>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>

HttpServer::HttpServer(int port, std::atomic<bool>* streamStartedFlag)
    : port_(port), streamStarted_(streamStartedFlag), server_fd_(-1), running_(false) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::start() {
    if (running_) {
        std::cerr << "[HttpServer] Already running, skipping start" << std::endl;
        return;
    }
    std::cerr << "[HttpServer] Starting server thread..." << std::endl;
    running_ = true;
    thread_ = std::thread(&HttpServer::run, this);
    std::cerr << "[HttpServer] Server thread launched" << std::endl;
}

void HttpServer::stop() {
    if (!running_) return;
    running_ = false;
    if (server_fd_ != -1) {
        close(server_fd_);
        server_fd_ = -1;
    }
    if (thread_.joinable()) thread_.join();
}

static std::string make_response(int status, const std::string& body, const std::string& contentType="application/json") {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " OK\r\n";
    oss << "Content-Type: " << contentType << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}

void HttpServer::run() {
    std::cerr << "[HttpServer] run() thread started" << std::endl;
    
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "[HttpServer] ERROR: Failed to create socket: " << strerror(errno) << std::endl;
        perror("[HttpServer] socket");
        running_ = false;
        return;
    }
    std::cerr << "[HttpServer] Socket created successfully (fd=" << server_fd_ << ")" << std::endl;

    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "[HttpServer] WARNING: setsockopt failed: " << strerror(errno) << std::endl;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    std::cerr << "[HttpServer] Attempting to bind to port " << port_ << "..." << std::endl;
    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[HttpServer] ERROR: Failed to bind to port " << port_ << ": " << strerror(errno) << std::endl;
        perror("[HttpServer] bind");
        close(server_fd_);
        running_ = false;
        return;
    }
    std::cerr << "[HttpServer] Bind successful" << std::endl;

    std::cerr << "[HttpServer] Starting to listen..." << std::endl;
    if (listen(server_fd_, 8) < 0) {
        std::cerr << "[HttpServer] ERROR: Failed to listen: " << strerror(errno) << std::endl;
        perror("[HttpServer] listen");
        close(server_fd_);
        running_ = false;
        return;
    }

    std::cerr << "[HttpServer] *** HTTP SERVER READY AND LISTENING ON PORT " << port_ << " ***" << std::endl;
    std::cerr << "[HttpServer] Waiting for connections..." << std::endl;

    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client < 0) {
            if (running_) {
                std::cerr << "[HttpServer] ERROR: accept failed: " << strerror(errno) << std::endl;
                perror("[HttpServer] accept");
            }
            break;
        }
        
        std::cerr << "[HttpServer] Connection received from client (fd=" << client << ")" << std::endl;

        // read a small request (we don't support very large headers)
        char buf[4096];
        ssize_t n = read(client, buf, sizeof(buf)-1);
        if (n <= 0) {
            close(client);
            continue;
        }
        buf[n] = '\0';
        std::string req(buf);

        bool is_stream_started = streamStarted_ && streamStarted_->load();

        std::string response_body;
        std::string response;

        if (req.find("GET /stream_started") == 0) {
            response_body = std::string("{\"started\":") + (is_stream_started ? "true" : "false") + "}\n";
            response = make_response(200, response_body);
            std::cerr << "[HttpServer] GET /stream_started -> {\"started\":" << (is_stream_started ? "true" : "false") << "}" << std::endl;
        } else if (req.find("GET /health") == 0) {
            response_body = "{\"ok\":true}\n";
            response = make_response(200, response_body);
            std::cerr << "[HttpServer] GET /health -> OK" << std::endl;
        } else {
            response_body = "Not Found\n";
            response = make_response(404, response_body, "text/plain");
            std::cerr << "[HttpServer] Unknown request, returning 404" << std::endl;
        }

        ssize_t written = write(client, response.c_str(), response.size());
        (void)written;
        close(client);
    }

    if (server_fd_ != -1) close(server_fd_);
    server_fd_ = -1;
}
