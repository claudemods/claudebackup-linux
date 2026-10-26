CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread
LDLIBS   := -pthread

TARGET := claudebackup-linux
HDRS   := $(wildcard src/*.h)

# The Arch ISO tree zip, embedded by src/resources.h with .incbin (the Calamares zips are used by build.sh)
ZIPS   := resources/build-image-arch-img.zip

$(TARGET): src/main.cpp $(HDRS) $(ZIPS)
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp $(LDLIBS)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)
	install -d $(DESTDIR)/etc/$(TARGET)
	install -Dm644 config/custom.exclude.list $(DESTDIR)/etc/$(TARGET)/custom.exclude.list

clean:
	rm -f $(TARGET)
	rm -rf build   # objects from the old ld -r -b binary build

.PHONY: install clean
