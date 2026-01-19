#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <ctime>
#include <thread>
#include <atomic>
#include <chrono>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

const char* STATUS_FILE = "/tmp/bambu_stream_status";
const int STATUS_TIMEOUT_SECONDS = 5;

// Cached in-memory status updated by a background thread.
static std::atomic<time_t> g_last_timestamp{0};
static std::atomic<bool> g_stream_active{false};
static const int POLL_INTERVAL_MS = 500; // background poll interval

// Non-blocking / fast path used by request handlers.
bool isStreamActive() {
    return g_stream_active.load(std::memory_order_relaxed);
}

// Background poller: reads the status file periodically and updates atomics.
void statusPoller() {
    while (true) {
        time_t last = 0;
        std::ifstream file(STATUS_FILE);
        if (file.is_open()) {
            std::string line;
            if (std::getline(file, line) && !line.empty()) {
                try {
                    last = static_cast<time_t>(std::stol(line));
                } catch (...) {
                    last = 0;
                }
            }
            file.close();
        }

        time_t now = time(nullptr);
        bool active = false;
        if (last != 0 && (now - last) < STATUS_TIMEOUT_SECONDS) {
            active = true;
        }

        g_last_timestamp.store(last, std::memory_order_relaxed);
        g_stream_active.store(active, std::memory_order_relaxed);

        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }
}

std::string makeResponse(int status, const std::string& body, const std::string& contentType = "application/json") {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " OK\r\n";
    oss << "Content-Type: " << contentType << "; charset=utf-8\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "Cache-Control: no-store, must-revalidate\r\n";
    oss << "Access-Control-Allow-Origin: *\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}

int main(int argc, char* argv[]) {
    int port = 8081;
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Invalid port, using default 8081" << std::endl;
            port = 8081;
        }
    }
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[StatusServer] ERROR: Failed to create socket" << std::endl;
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[StatusServer] ERROR: Failed to bind to port " << port << std::endl;
        close(server_fd);
        return 1;
    }
    
    if (listen(server_fd, 8) < 0) {
        std::cerr << "[StatusServer] ERROR: Failed to listen" << std::endl;
        close(server_fd);
        return 1;
    }
    
    std::cerr << "========================================" << std::endl;
    std::cerr << "[StatusServer] HTTP Status Server Started" << std::endl;
    std::cerr << "[StatusServer] Listening on port " << port << std::endl;
    std::cerr << "[StatusServer] Endpoints:" << std::endl;
    std::cerr << "[StatusServer]   - GET /stream_started" << std::endl;
    std::cerr << "[StatusServer]   - GET /health" << std::endl;
    std::cerr << "========================================" << std::endl;
    
    // Start background poller to keep a cached status in memory.
    std::thread(statusPoller).detach();

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client < 0) {
            continue;
        }
        
        // Set a short receive timeout so the handler never blocks indefinitely.
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200 * 1000; // 200ms
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

        char buf[4096];
        ssize_t n = recv(client, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            shutdown(client, SHUT_RDWR);
            close(client);
            continue;
        }
        buf[n] = '\0';
        std::string req(buf);
        
        std::string response;
        
        if (req.find("GET /stream_started") == 0) {
            // Fast, non-blocking read from cached state
            bool active = isStreamActive();
            std::string body = std::string("{\"started\":") + (active ? "true" : "false") + "}\n";
            response = makeResponse(200, body);
            std::cerr << "[StatusServer] GET /stream_started -> {\"started\":" << (active ? "true" : "false") << "}" << std::endl;
        } else if (req.find("GET /health") == 0) {
            std::string body = "{\"ok\":true}\n";
            response = makeResponse(200, body);
        } else {
            std::string body = "Not Found\n";
            response = makeResponse(404, body, "text/plain");
        }
        
        // Write response and ensure it's flushed and connection closed promptly.
        ssize_t written = write(client, response.c_str(), response.size());
        (void)written;
        shutdown(client, SHUT_WR);
        close(client);
    }
    
    close(server_fd);
    return 0;
}
