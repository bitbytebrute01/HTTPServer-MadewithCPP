#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <functional>
#include <unordered_map>
#include <atomic>

#include "http_parser.h"

// A route handler takes a parsed request and returns a response.
using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;

// ===========================================================================
// HttpServer — a simple, thread-per-connection HTTP/1.1 server
//
// Usage:
//   HttpServer server(8080);
//   server.get("/",           handler_root);
//   server.get("/api/status", handler_status);
//   server.post("/api/echo",  handler_echo);
//   server.start();   // blocks until stop() is called
// ===========================================================================
class HttpServer {
public:
    // Create a server that will listen on `port` and serve static files
    // from `static_dir` (relative to the working directory).
    explicit HttpServer(uint16_t port, const std::string& static_dir = "public");
    ~HttpServer();

    // --- Route registration ---
    void get(const std::string& path, RouteHandler handler);
    void post(const std::string& path, RouteHandler handler);

    // Start accepting connections.  Blocks the calling thread.
    void start();

    // Signal the server to stop (safe to call from a signal handler).
    void stop();

private:
    uint16_t    port_;
    int         server_fd_ = -1;
    std::string static_dir_;
    std::atomic<bool> running_{false};

    // Nested map: routes_["GET"]["/api/status"] = handler
    std::unordered_map<std::string,
        std::unordered_map<std::string, RouteHandler>> routes_;

    // --- Internal helpers ---

    // Create, configure, bind, and listen on the server socket.
    void setup_socket();

    // Handle one client connection (runs in its own thread).
    void handle_client(int client_fd);

    // Read the full HTTP request from a socket, respecting Content-Length.
    std::string read_request(int client_fd);

    // Dispatch a parsed request to the matching handler or static file.
    HttpResponse route_request(const HttpRequest& request);

    // Try to serve a file from the static directory.
    HttpResponse serve_static(const std::string& url_path);

    // Map a file extension to its MIME content type.
    static std::string content_type_for(const std::string& path);
};

#endif // SERVER_H
