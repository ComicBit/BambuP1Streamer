#include "HttpServer.h"

#include <atomic>
#include <cstring>
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
    if (running_) return;
    running_ = true;
    thread_ = std::thread(&HttpServer::run, this);
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
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        perror("socket");
        running_ = false;
        return;
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd_);
        running_ = false;
        return;
    }

    if (listen(server_fd_, 8) < 0) {
        perror("listen");
        close(server_fd_);
        running_ = false;
        return;
    }

    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client < 0) {
            if (running_) perror("accept");
            break;
        }

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
        } else if (req.find("GET /health") == 0) {
            response_body = "{\"ok\":true}\n";
            response = make_response(200, response_body);
        } else {
            response_body = "Not Found\n";
            response = make_response(404, response_body, "text/plain");
        }

        ssize_t written = write(client, response.c_str(), response.size());
        (void)written;
        close(client);
    }

    if (server_fd_ != -1) close(server_fd_);
    server_fd_ = -1;
}
