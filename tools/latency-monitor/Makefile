CXX = g++
CXXFLAGS = -O3 -std=c++17 -I../../include

all: latency_monitor

latency_monitor: latency_monitor.cpp
	$(CXX) $(CXXFLAGS) latency_monitor.cpp -o latency_monitor

clean:
	rm -f latency_monitor