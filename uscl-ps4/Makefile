OO_PS4_TOOLCHAIN ?= $(HOME)/ps4dev/OpenOrbis/PS4Toolchain
export OO_PS4_TOOLCHAIN
JBC_SOURCE ?= $(HOME)/ps4dev/libjbc
PS4_CXX ?= clang++
PS4_CC ?= clang
PS4_LD ?= ld.lld
HOST_CXX ?= c++
PKGTOOL ?= $(OO_PS4_TOOLCHAIN)/bin/linux/PkgTool.Core
COMMON := src/library.cpp src/canvas.cpp src/main.cpp
PS4_OBJECTS := $(patsubst src/%.cpp,build/ps4/%.o,$(COMMON) src/ps4.cpp)
JBC_OBJECTS := $(addprefix build/jbc/,jailbreak.o kernelrw.o utils.o)
TARGET_FLAGS := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -D_POSIX_C_SOURCE=200809L -isysroot $(OO_PS4_TOOLCHAIN) -isystem $(OO_PS4_TOOLCHAIN)/include
CXX_FLAGS := $(TARGET_FLAGS) -isystem $(OO_PS4_TOOLCHAIN)/include/c++/v1 -std=c++17 -O2 -Wall -Wextra -I$(JBC_SOURCE)
LIBS := -lc++ -lSceVideoOut -lSceSysmodule -lSceFreeType -lScePad -lSceUserService -lSceLncUtil -lSceSystemService -lc -lkernel
CONTENT_ID := IV0000-USCL00001_00-USCLNATIVEPS4001
HOST_FLAGS := -std=c++17 -O2 -Wall -Wextra $(shell pkg-config --cflags sdl2 freetype2)
HEADERS := $(wildcard src/*.hpp) third_party/stb_image.h

.PHONY: all assets preview test package validate clean
all: package

assets:
	python3 scripts/assets.py

build/ps4/%.o: src/%.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(PS4_CXX) $(CXX_FLAGS) -MMD -MP -c $< -o $@

build/jbc/%.o: $(JBC_SOURCE)/%.c
	@mkdir -p $(@D)
	$(PS4_CC) $(TARGET_FLAGS) -O2 -ffreestanding -mno-red-zone -c $< -o $@

build/uscl-ps4.elf: $(PS4_OBJECTS) $(JBC_OBJECTS)
	$(PS4_LD) -m elf_x86_64 -pie --script $(OO_PS4_TOOLCHAIN)/link.x --eh-frame-hdr -L$(OO_PS4_TOOLCHAIN)/lib -o $@ $^ $(OO_PS4_TOOLCHAIN)/lib/crt1.o $(LIBS)

build/stage/eboot.bin: build/uscl-ps4.elf
	@mkdir -p $(@D)
	$(OO_PS4_TOOLCHAIN)/bin/linux/create-fself -in=$< -out=build/uscl-ps4.oelf --eboot $@ --paid 0x3800000000000011

package: assets build/stage/eboot.bin
	DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 python3 scripts/package.py --sdk '$(OO_PS4_TOOLCHAIN)' --pkgtool '$(PKGTOOL)'

validate:
	DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 '$(PKGTOOL)' pkg_validate build/$(CONTENT_ID).pkg
	python3 scripts/validate.py build/$(CONTENT_ID).pkg build/stage/sce_sys/param.sfo build/uscl-ps4.elf

build/uscl-ps4-preview: $(COMMON) src/desktop.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(HOST_CXX) $(HOST_FLAGS) -DUSCL_HOST $(COMMON) src/desktop.cpp -o $@ $(shell pkg-config --libs sdl2 freetype2)

preview: assets build/uscl-ps4-preview
	./build/uscl-ps4-preview

build/library-test: tests/library_test.cpp src/library.cpp src/library.hpp
	@mkdir -p $(@D)
	$(HOST_CXX) -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer tests/library_test.cpp src/library.cpp -o $@

test: build/library-test
	./build/library-test
	python3 -m unittest discover -s tests -p '*_test.py' -v

clean:
	rm -rf build

-include $(PS4_OBJECTS:.o=.d)
