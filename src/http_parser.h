#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// HttpRequest — represents a parsed incoming HTTP request
// ---------------------------------------------------------------------------
struct HttpRequest {
    std::string method;   // GET, POST, PUT, DELETE, …
    std::string path;     // /api/status, /index.html, …
    std::string version;  // HTTP/1.1
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

// ---------------------------------------------------------------------------
// HttpResponse — represents an outgoing HTTP response
// ---------------------------------------------------------------------------
struct HttpResponse {
    int         status_code = 200;
    std::string status_text = "OK";
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    // -- Factory methods for common responses --

    static HttpResponse ok(const std::string& body,
                           const std::string& content_type = "text/html");
    static HttpResponse json(const std::string& json_body);
    static HttpResponse not_found();
    static HttpResponse bad_request(const std::string& message = "Bad Request");
    static HttpResponse internal_error();

    // Serialize the response into a raw HTTP/1.1 response string that can be
    // written directly to the socket.
    std::string serialize() const;
};

// ---------------------------------------------------------------------------
// parse_request — turn raw bytes into an HttpRequest struct
// Returns a request with an empty method on parse failure.
// ---------------------------------------------------------------------------
HttpRequest parse_request(const std::string& raw);

#endif // HTTP_PARSER_H
