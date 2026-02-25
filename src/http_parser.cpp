#include "http_parser.h"
#include <sstream>
#include <algorithm>

HttpResponse HttpResponse::ok(const std::string& body,
                              const std::string& content_type) {
    HttpResponse r;
    r.status_code = 200;
    r.status_text = "OK";
    r.body = body;
    r.headers["Content-Type"] = content_type;
    r.headers["Content-Length"] = std::to_string(body.size());
    return r;
}

HttpResponse HttpResponse::json(const std::string& json_body) {
    return ok(json_body, "application/json");
}

HttpResponse HttpResponse::not_found() {
    HttpResponse r;
    r.status_code = 404;
    r.status_text = "Not Found";
    r.body = R"({"error": "Not Found"})";
    r.headers["Content-Type"] = "application/json";
    r.headers["Content-Length"] = std::to_string(r.body.size());
    return r;
}

HttpResponse HttpResponse::bad_request(const std::string& message) {
    HttpResponse r;
    r.status_code = 400;
    r.status_text = "Bad Request";
    r.body = R"({"error": ")" + message + R"("})";
    r.headers["Content-Type"] = "application/json";
    r.headers["Content-Length"] = std::to_string(r.body.size());
    return r;
}

HttpResponse HttpResponse::internal_error() {
    HttpResponse r;
    r.status_code = 500;
    r.status_text = "Internal Server Error";
    r.body = R"({"error": "Internal Server Error"})";
    r.headers["Content-Type"] = "application/json";
    r.headers["Content-Length"] = std::to_string(r.body.size());
    return r;
}

std::string HttpResponse::serialize() const {
    std::ostringstream out;
    out << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    for (const auto& [key, value] : headers) {
        out << key << ": " << value << "\r\n";
    }
    if (headers.find("Connection") == headers.end()) {
        out << "Connection: close\r\n";
    }
    out << "\r\n";
    out << body;
    return out.str();
}

HttpRequest parse_request(const std::string& raw) {
    HttpRequest req;
    size_t line_end = raw.find("\r\n");
    if (line_end == std::string::npos) {
        return req;
    }

    std::istringstream line_stream(raw.substr(0, line_end));
    line_stream >> req.method >> req.path >> req.version;

    if (req.method.empty() || req.path.empty()) {
        req.method.clear();
        return req;
    }

    size_t pos = line_end + 2;
    while (pos < raw.size()) {
        size_t next_end = raw.find("\r\n", pos);
        if (next_end == std::string::npos) break;
        if (next_end == pos) {
            pos = next_end + 2;
            break;
        }
        std::string header_line = raw.substr(pos, next_end - pos);
        size_t colon = header_line.find(':');
        if (colon != std::string::npos) {
            std::string key = header_line.substr(0, colon);
            std::string value = header_line.substr(colon + 1);
            size_t start = value.find_first_not_of(" \t");
            if (start != std::string::npos) {
                value = value.substr(start);
            }
            req.headers[key] = value;
        }
        pos = next_end + 2;
    }

    if (pos < raw.size()) {
        req.body = raw.substr(pos);
    }

    return req;
}