TARGET_TRIPLE = x86_64-apple-darwin
CXX = clang++
LD = ld.lld -flavor darwinnew

CXXFLAGS = -target $(TARGET_TRIPLE) \
           -mkernel -fapple-kext \
           -nostdlib -fno-builtin -fno-rtti -fno-exceptions \
           -fno-common \
           -O2 -Wall -Wextra \
           -I../../kernel/iokit -I../../kernel/libkern -I../../kernel/osfmk -I../../kernel/bsd

LDFLAGS = -r -Wl,-kext \
          -undefined dynamic_lookup

KEXT_NAME = NullCPUPowerManagement
KEXT_BIN = $(KEXT_NAME).kext/Contents/MacOS/$(KEXT_NAME)
KEXT_OBJS = src/NullCPUPowerManagement.o

all: $(KEXT_BIN)

$(KEXT_BIN): $(KEXT_OBJS)
	mkdir -p $(KEXT_NAME).kext/Contents/MacOS
	$(LD) $(LDFLAGS) -o $@ $(KEXT_OBJS)
	cp src/Info.plist $(KEXT_NAME).kext/Contents/
	@echo "build: KEXT bundle $(KEXT_NAME)"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(KEXT_OBJS)
	rm -rf $(KEXT_NAME).kext
