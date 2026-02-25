#include "server.h"
#include <iostream>
#include <csignal>

static HttpServer* g_server = nullptr;

static void signal_handler(int signum) {
    std::cout << "\nShutting down..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    uint16_t port = 8080;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    HttpServer server(port);
    g_server = &server;

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    server.get("/", [](const HttpRequest&) {
        return HttpResponse::ok(R"(
<!DOCTYPE html>
<html>
<body>
<h1>Hello World</h1>
<h2>I am Aditya Joshi</h2>
</body>
</html>
)");
    });

    server.get("/api/status", [](const HttpRequest&) {
        return HttpResponse::json(R"({"status":"ok"})");
    });

    server.post("/api/echo", [](const HttpRequest& req) {
        if (req.body.empty()) {
            return HttpResponse::bad_request("Empty body");
        }
        return HttpResponse::json(req.body);
    });

    server.start();
    return 0;
}