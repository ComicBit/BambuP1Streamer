#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

const char* STATUS_FILE = "/tmp/bambu_stream_status";
const int STATUS_TIMEOUT_SECONDS = 5;

bool isStreamActive() {
    std::ifstream file(STATUS_FILE);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    std::getline(file, line);
    file.close();
    
    if (line.empty()) {
        return false;
    }
    
    // Parse timestamp from file
    time_t timestamp = std::stol(line);
    time_t now = time(nullptr);
    
    // Stream is active if updated within last 5 seconds
    return (now - timestamp) < STATUS_TIMEOUT_SECONDS;
}

std::string makeResponse(int status, const std::string& body, const std::string& contentType = "application/json") {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " OK\r\n";
    oss << "Content-Type: " << contentType << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n";
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
    
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client < 0) {
            continue;
        }
        
        char buf[4096];
        ssize_t n = read(client, buf, sizeof(buf) - 1);
        if (n <= 0) {
            close(client);
            continue;
        }
        buf[n] = '\0';
        std::string req(buf);
        
        std::string response;
        
        if (req.find("GET /stream_started") == 0) {
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
        
        write(client, response.c_str(), response.size());
        close(client);
    }
    
    close(server_fd);
    return 0;
}
