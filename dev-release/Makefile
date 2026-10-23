CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread
LDLIBS   := -lcrypt -pthread

TARGET := claudebackup-linux
HDRS   := $(wildcard src/*.h)

$(TARGET): src/main.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp $(LDLIBS)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)
	install -d $(DESTDIR)/etc/$(TARGET)
	install -Dm644 config/custom.exclude.list $(DESTDIR)/etc/$(TARGET)/custom.exclude.list

clean:
	rm -f $(TARGET)

.PHONY: install clean
