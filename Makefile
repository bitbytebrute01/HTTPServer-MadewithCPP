# ============================================================================
# Makefile — C++ HTTP Server
#
# Usage:
#   make          Build the server binary
#   make run      Build and run on port 8080
#   make clean    Remove build artifacts
# ============================================================================

CXX       ?= g++
CXXFLAGS   = -std=c++17 -Wall -Wextra -Wpedantic -O2 -pthread

SRC_DIR    = src
BUILD_DIR  = build
TARGET     = http_server

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

# --- Targets ----------------------------------------------------------------

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

run: $(TARGET)
	./$(TARGET) 8080
