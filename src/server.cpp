#include "server.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <cstring>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
static constexpr size_t MAX_REQUEST_SIZE = 1 * 1024 * 1024;  // 1 MB
static constexpr size_t BUFFER_SIZE      = 8192;              // recv chunk
static constexpr int    READ_TIMEOUT_SEC = 5;                 // per-client

// ===========================================================================
// Construction / destruction
// ===========================================================================

HttpServer::HttpServer(uint16_t port, const std::string& static_dir)
    : port_(port), static_dir_(static_dir) {}

HttpServer::~HttpServer() {
    stop();
}

// ===========================================================================
// Route registration — store handlers in the nested map
// ===========================================================================

void HttpServer::get(const std::string& path, RouteHandler handler) {
    routes_["GET"][path] = std::move(handler);
}

void HttpServer::post(const std::string& path, RouteHandler handler) {
    routes_["POST"][path] = std::move(handler);
}

// ===========================================================================
// setup_socket — create, configure, bind, and listen
//
// The socket workflow:
//
// 1. socket()      → allocate a file descriptor for the TCP endpoint
// 2. setsockopt()  → SO_REUSEADDR lets us restart immediately after a crash
// 3. bind()        → associate the socket with an address (0.0.0.0:port)
// 4. listen()      → mark the socket as passive (it will accept connections)
//
// After this, the socket is ready for accept() calls.
// ===========================================================================

void HttpServer::setup_socket() {
    // 1. Create a TCP (SOCK_STREAM) socket using IPv4 (AF_INET)
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        throw std::runtime_error(
            "socket() failed: " + std::string(strerror(errno)));
    }

    // 2. Allow reusing the port immediately after the server stops.
    //    Without this, you'd get "Address already in use" for ~60 seconds
    //    after a restart while the OS holds the port in TIME_WAIT.
    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(server_fd_);
        throw std::runtime_error(
            "setsockopt(SO_REUSEADDR) failed: " + std::string(strerror(errno)));
    }

    // 3. Bind to all network interfaces on the specified port.
    //    sin_addr = INADDR_ANY means "listen on every interface" (0.0.0.0).
    //    htons() converts the port number from host byte order to network byte
    //    order (big-endian), which is required by the socket API.
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);

    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(server_fd_);
        throw std::runtime_error(
            "bind() failed on port " + std::to_string(port_) + ": "
            + std::string(strerror(errno)));
    }

    // 4. Start listening.  The backlog (128) is how many pending connections
    //    the kernel will queue before refusing new ones.
    if (listen(server_fd_, 128) < 0) {
        close(server_fd_);
        throw std::runtime_error(
            "listen() failed: " + std::string(strerror(errno)));
    }
}

// ===========================================================================
// start — the main accept loop
//
// Concurrency model: thread-per-connection
//
// For each accepted client, we spawn a detached std::thread that:
//   1. Reads the HTTP request
//   2. Parses it
//   3. Routes it to a handler (or static file)
//   4. Sends the response
//   5. Closes the connection
//
// Detached threads clean themselves up when they finish — we don't need
// to join them.  This is simple and works well for moderate load.  For
// very high concurrency you'd use epoll (Linux) or kqueue (macOS) with
// a thread pool, but that's beyond the scope of this project.
// ===========================================================================

void HttpServer::start() {
    setup_socket();
    running_ = true;

    std::cout << "Server listening on http://localhost:" << port_ << std::endl;

    while (running_) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);

        // accept() blocks until a client connects.  It returns a NEW file
        // descriptor for the client connection — the original server_fd_
        // keeps listening for more clients.
        int client_fd = accept(
            server_fd_,
            reinterpret_cast<sockaddr*>(&client_addr),
            &client_len);

        if (client_fd < 0) {
            if (!running_) break;  // server is shutting down
            std::cerr << "accept() failed: " << strerror(errno) << std::endl;
            continue;
        }

        // Log the connection (inet_ntop is the modern, thread-safe way to
        // convert an IP address to a string).
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        std::cout << "Connection from " << ip_str
                  << ":" << ntohs(client_addr.sin_port) << std::endl;

        // Spawn a thread to handle this client.  We capture client_fd by
        // value so each thread has its own copy.
        std::thread([this, client_fd]() {
            handle_client(client_fd);
        }).detach();
    }
}

// ===========================================================================
// stop — signal the server to shut down
// ===========================================================================

void HttpServer::stop() {
    running_ = false;
    if (server_fd_ >= 0) {
        close(server_fd_);   // unblocks accept() in the main loop
        server_fd_ = -1;
    }
}

// ===========================================================================
// read_request — read the full HTTP request from the client socket
//
// Strategy:
// 1. Read in BUFFER_SIZE chunks
// 2. After each chunk, check whether we've received "\r\n\r\n" (end of
//    headers)
// 3. If there's a Content-Length header, keep reading until the body is
//    complete
// 4. Apply a read timeout so slow/dead clients don't hold threads forever
// ===========================================================================

std::string HttpServer::read_request(int client_fd) {
    // Set a per-socket read timeout.  If the client stops sending data,
    // recv() will return -1 with errno = EAGAIN after this many seconds.
    struct timeval tv{};
    tv.tv_sec  = READ_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::string data;
    char buffer[BUFFER_SIZE];

    while (data.size() < MAX_REQUEST_SIZE) {
        ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);
        if (n <= 0) break;  // error, timeout, or client closed

        data.append(buffer, static_cast<size_t>(n));

        // Have we received the end-of-headers marker?
        size_t header_end = data.find("\r\n\r\n");
        if (header_end == std::string::npos) continue;

        // Determine the expected body length from Content-Length.
        // We do a case-insensitive search since header names are
        // case-insensitive per HTTP/1.1 (RFC 7230 §3.2).
        size_t body_start     = header_end + 4;
        size_t content_length = 0;

        std::string lower_headers = data.substr(0, header_end);
        std::transform(lower_headers.begin(), lower_headers.end(),
                       lower_headers.begin(), ::tolower);

        size_t cl_pos = lower_headers.find("content-length:");
        if (cl_pos != std::string::npos) {
            size_t val_start = cl_pos + 15;
            size_t val_end   = lower_headers.find("\r\n", val_start);
            if (val_end == std::string::npos) val_end = lower_headers.size();

            std::string cl_str = data.substr(val_start, val_end - val_start);
            size_t first_digit = cl_str.find_first_of("0123456789");
            if (first_digit != std::string::npos) {
                try {
                    content_length = std::stoul(cl_str.substr(first_digit));
                } catch (...) {
                    break;  // malformed Content-Length
                }
            }
        }

        // Check whether we've received the full body
        if (data.size() - body_start >= content_length) break;
    }

    return data;
}

// ===========================================================================
// handle_client — process one connection end-to-end
// ===========================================================================

void HttpServer::handle_client(int client_fd) {
    std::string raw = read_request(client_fd);

    HttpResponse response;

    if (raw.empty()) {
        response = HttpResponse::bad_request("Empty request");
    } else {
        HttpRequest request = parse_request(raw);

        if (request.method.empty()) {
            response = HttpResponse::bad_request("Malformed request");
        } else {
            // Log the request line
            std::cout << request.method << " " << request.path << std::endl;
            response = route_request(request);
        }
    }

    // Send the full response.  For the small responses we produce, a
    // single send() call is sufficient.  A production server would loop
    // to handle partial writes.
    std::string wire = response.serialize();
    send(client_fd, wire.c_str(), wire.size(), 0);

    // Close the connection.  We always set "Connection: close" so the
    // client knows not to send another request on this socket.
    close(client_fd);
}

// ===========================================================================
// route_request — dispatch to registered handler or static file
//
// Lookup order:
// 1. Check routes_[method][path] — if found, call the handler
// 2. For GET requests, try serving a static file from static_dir_
// 3. If nothing matches, return 404
// ===========================================================================

HttpResponse HttpServer::route_request(const HttpRequest& request) {
    // 1. Check registered routes
    auto method_it = routes_.find(request.method);
    if (method_it != routes_.end()) {
        auto path_it = method_it->second.find(request.path);
        if (path_it != method_it->second.end()) {
            try {
                return path_it->second(request);
            } catch (const std::exception& e) {
                std::cerr << "Handler error: " << e.what() << std::endl;
                return HttpResponse::internal_error();
            }
        }
    }

    // 2. Fall back to static file serving (GET only)
    if (request.method == "GET") {
        HttpResponse file_resp = serve_static(request.path);
        if (file_resp.status_code != 404) {
            return file_resp;
        }
    }

    // 3. Nothing matched
    return HttpResponse::not_found();
}

// ===========================================================================
// serve_static — serve a file from the public/ directory
//
// Maps the URL path to a filesystem path:
//   /              → public/index.html
//   /style.css     → public/style.css
//   /js/app.js     → public/js/app.js
//
// Security: rejects any path containing ".." to prevent directory traversal
// attacks (e.g., GET /../../etc/passwd).
// ===========================================================================

HttpResponse HttpServer::serve_static(const std::string& url_path) {
    // Block directory traversal attempts
    if (url_path.find("..") != std::string::npos) {
        return HttpResponse::bad_request("Invalid path");
    }

    // Build the filesystem path
    std::string file_path = static_dir_ + url_path;

    // If the path points to a directory, look for index.html
    if (!file_path.empty() && file_path.back() == '/') {
        file_path += "index.html";
    }

    // Try to open the file in binary mode
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return HttpResponse::not_found();
    }

    // Read the entire file into a string
    std::ostringstream oss;
    oss << file.rdbuf();
    std::string content = oss.str();

    return HttpResponse::ok(content, content_type_for(file_path));
}

// ===========================================================================
// content_type_for — map file extension to MIME type
// ===========================================================================

std::string HttpServer::content_type_for(const std::string& path) {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";

    std::string ext = path.substr(dot);

    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".css")                   return "text/css";
    if (ext == ".js")                    return "application/javascript";
    if (ext == ".json")                  return "application/json";
    if (ext == ".png")                   return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif")                   return "image/gif";
    if (ext == ".svg")                   return "image/svg+xml";
    if (ext == ".ico")                   return "image/x-icon";
    if (ext == ".txt")                   return "text/plain";

    return "application/octet-stream";
}
