# C++ HTTP Server (From Scratch)

This project is a simple HTTP/1.1 server written in C++ using POSIX sockets
and the C++ standard library.

I built this project to understand how web servers actually work internally,
instead of relying only on frameworks like Express, Flask, or Django.

The focus of this project is learning how HTTP requests are received, parsed,
routed, and responded to at a low level.

---

## Why I built this

As someone learning web development, I realized that most frameworks hide
what happens behind the scenes.

This project explores:
- how a browser connects to a server
- how HTTP requests are structured
- how routing works internally
- how responses are sent back to clients

Instead of using a complex language or framework, I wanted to see how much
can be done using plain C++ and system libraries.

---

## What this server does

- Listens on a TCP port (default: 8080)
- Accepts multiple client connections
- Parses HTTP/1.1 requests manually
- Supports GET and POST requests
- Routes requests to handlers
- Serves static files from a `public/` directory
- Returns JSON and HTML responses
- Handles multiple clients using threads

---

## Example routes

| Method | Path | Description |
|------|------|------------|
| GET | `/` | Returns a simple HTML page |
| GET | `/api/status` | Returns server status as JSON |
| POST | `/api/echo` | Returns the request body |

---

## Project structure# HTTPServer-MadewithCPP
.
├── main.cpp
├── server.h
├── server.cpp
├── http_parser.h
├── http_parser.cpp
├── public/
│   └── index.html

---

## How it works (high level)

1. The server opens a TCP socket and listens on a port
2. A browser connects to the server
3. The raw HTTP request is read from the socket
4. The request is parsed into method, path, headers, and body
5. The request is routed to a matching handler
6. A response is created and serialized
7. The response is sent back to the browser
8. The connection is closed

---

## Build and run

Compile the server:

```bash
g++ -std=c++17 main.cpp server.cpp http_parser.cpp -o http_server -pthread

## Run the Server

./http_server

## Open your browser and visit:

http://localhost:8080