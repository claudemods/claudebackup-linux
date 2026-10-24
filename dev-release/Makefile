CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread
LDLIBS   := -pthread

TARGET := claudebackup-linux
HDRS   := $(wildcard src/*.h)

# claudemods zips (Arch ISO tree + Calamares) embedded into the binary
ZIPS   := build-image-arch-img calamares-files claudemods
RESOBJ := $(patsubst %,build/%.o,$(ZIPS))

$(TARGET): src/main.cpp $(HDRS) $(RESOBJ)
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp $(RESOBJ) $(LDLIBS)

# Run from resources/ so the symbols are _binary_<name>_zip_start/_end
build/%.o: resources/%.zip
	@mkdir -p build
	cd resources && $(LD) -r -b binary -z noexecstack -o ../$@ $*.zip

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)
	install -d $(DESTDIR)/etc/$(TARGET)
	install -Dm644 config/custom.exclude.list $(DESTDIR)/etc/$(TARGET)/custom.exclude.list

clean:
	rm -f $(TARGET)
	rm -rf build

.PHONY: install clean
