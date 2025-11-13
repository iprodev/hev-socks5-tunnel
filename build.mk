# Build configuration with performance optimizations

# Version
VERSION_CFLAGS := -DMAJOR_VERSION=2 -DMINOR_VERSION=0 -DMICRO_VERSION=0 -DCOMMIT_ID=\"optimized\"

# Performance optimizations flags
ENABLE_OPTIMIZATIONS ?= 1

ifeq ($(ENABLE_OPTIMIZATIONS),1)
	CCFLAGS += -DENABLE_OPTIMIZATIONS
	CCFLAGS += -march=native -mtune=native
	CCFLAGS += -flto
	LDFLAGS += -flto
endif

# SIMD support (auto-detected)
CCFLAGS += -mavx2 -msse4.2

# All source files
SRCFILES := \
	$(SRCDIR)/hev-main.c \
	$(SRCDIR)/hev-config.c \
	$(SRCDIR)/hev-socks5-tunnel.c \
	$(SRCDIR)/hev-socks5-session.c \
	$(SRCDIR)/hev-socks5-session-tcp.c \
	$(SRCDIR)/hev-socks5-session-udp.c \
	$(SRCDIR)/hev-mapped-dns.c \
	$(SRCDIR)/hev-thread-pool.c \
	$(SRCDIR)/hev-tunnel-io.c \
	$(SRCDIR)/hev-tunnel-linux.c \
	$(SRCDIR)/hev-tunnel-freebsd.c \
	$(SRCDIR)/hev-tunnel-macos.c \
	$(SRCDIR)/hev-tunnel-netbsd.c \
	$(SRCDIR)/hev-tunnel-windows.c \
	$(SRCDIR)/misc/hev-compiler.c \
	$(SRCDIR)/misc/hev-logger.c

# Performance optimization modules
ifeq ($(ENABLE_OPTIMIZATIONS),1)
	SRCFILES += \
		$(SRCDIR)/hev-ring-buffer.c \
		$(SRCDIR)/hev-memory-pool.c \
		$(SRCDIR)/hev-simd.c \
		$(SRCDIR)/hev-connection-pool.c \
		$(SRCDIR)/hev-io-uring.c \
		$(SRCDIR)/hev-cpu-affinity.c \
		$(SRCDIR)/hev-ebpf-filter.c \
		$(SRCDIR)/hev-adaptive-pool.c \
		$(SRCDIR)/hev-tunnel-io-enhanced.c
	
	# Additional libraries
	LDFLAGS += -luring -lnuma
endif

# JNI support (Android)
ifneq ($(ANDROID_NDK_ROOT),)
	SRCFILES += $(SRCDIR)/hev-jni.c
endif
