TARGET = ytpavlov_mc_ios.dylib
CC = xcrun -sdk iphoneos clang++
CFLAGS = -arch arm64 -miphoneos-version-min=14.0 -std=c++17 -shared -O2
LDFLAGS = -framework Foundation -framework UIKit -framework CoreGraphics -framework QuartzCore

SRCS = main.mm hooks.cpp menu.mm overlay.mm
HEADERS = memory.hpp sdk.hpp hooks.hpp menu.h overlay.h

all: $(TARGET)

$(TARGET): $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)
