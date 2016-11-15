CXXFLAGS?=
LDFLAGS?=

# Append dash
ifdef PREFIX
xPREFIX = $(PREFIX)-
endif

# Check if we are compiling for Windows
ifeq ($(OS),Windows_NT)
# However, if PREFIX is set, it's possible that we are cross-compiling, so don't set it if prefix is set
ifndef PREFIX
RC_CMD := windres -O coff VersionInfo.rc VersionInfo.res
RC_FILE := VersionInfo.res
else
ifneq (,$(findstring mingw32,$(PREFIX)))
RC_CMD := $(xPREFIX)windres -O coff VersionInfo.rc VersionInfo.res
RC_FILE := VersionInfo.res
else
RC_CMD :=
RC_FILE :=
endif
endif
else
ifneq (,$(findstring mingw32,$(PREFIX)))
RC_CMD := $(xPREFIX)windres -O coff VersionInfo.rc VersionInfo.res
RC_FILE := VersionInfo.res
# MinGW32 Cross compiler doesn't automatically append .exe
EXTENSION_APPEND := .exe
else
RC_CMD :=
RC_FILE :=
endif
endif

# Debug flags
RELEASE_GCC_CMD := -O3
RELEASE_MSV_CMD := -Ox -MT
DEBUG_GCC_CMD :=
DEBUG_MSV_CMD :=
NDK_DEBUG :=

# Files
GCC_FILES=midi2sif.o
MSVC_FILES=midi2sif.obj

# Rules

all: midi2sif

debug:
	@echo Debug build.
	$(eval RELEASE_GCC_CMD = -O0)
	$(eval RELEASE_MSV_CMD = -Od -D"_DEBUG" -MTd)
	$(eval DEBUG_GCC_CMD = -g -D_DEBUG)
	$(eval DEBUG_MSV_CMD = -PDB:"bin\\midi2sif.pdb" -DEBUG)
	$(eval NDK_DEBUG = NDK_DEBUG=1)

midi2sif: $(GCC_FILES)
	-mkdir -p bin
	$(RC_CMD)
	$(xPREFIX)g++ $(RELEASE_GCC_CMD) $(DEBUG_GCC_CMD) -o bin/midi2sif$(EXTENSION_APPEND) $(CXXFLAGS) $(LDFLAGS) $(GCC_FILES) $(RC_FILE)
	-rm $(GCC_FILES) $(RC_FILE)

prevscmd:
	@echo "VSCMD build verify"
	where cl.exe
	where link.exe
	@echo "VSCMD verify OK"

vscmd: prevscmd $(MSVC_FILES)
	-mkdir -p bin/vscmd
	link -OUT:"bin\\midi2sif.exe" -NXCOMPAT $(DEBUG_MSV_CMD) -RELEASE -SUBSYSTEM:CONSOLE $(LDFLAGS) $(MSVC_FILES)
	-rm $(MSVC_FILES)


clean:
	-@rm $(GCC_FILES) $(MSVC_FILES) $(RC_FILE)
	-@rm -R obj
	-@rm -R libs

# Object files
# .o for GCC
# .obj for MSVC
midi2sif.o:
	$(xPREFIX)g++ -c $(RELEASE_GCC_CMD) $(DEBUG_GCC_CMD) $(CXXFLAGS) src/midi2sif.cpp

midi2sif.obj:
	cl -nologo -W3 -Zc:wchar_t -Za $(RELEASE_MSV_CMD) -wd"4996" -D"WIN32" -D"_CONSOLE" -EHsc -c $(CFLAGS) src/midi2sif.cpp

.PHONY: all midi2sif debug prevscmd vscmd clean
