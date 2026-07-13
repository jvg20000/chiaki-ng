FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive

# ── System deps ──────────────────────────────────────────────
RUN apt-get update && apt-get install -y --no-install-recommends \
    sudo \
    # Build tools
    build-essential cmake ninja-build pkg-config \
    git curl wget ca-certificates gnupg \
    # Qt6 (Core Gui Concurrent Svg Qml Quick Widgets WebSockets WebEngine DBus)
    qt6-base-dev \
    qt6-declarative-dev \
    qt6-svg-dev \
    qt6-websockets-dev \
    qt6-webengine-dev \
    qt6-webengine-dev-tools \
    qt6-tools-dev qt6-tools-dev-tools \
    qt6-base-private-dev \
    libgl1-mesa-dev libvulkan-dev \
    # Multimedia / FFMPEG
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
    libavcodec-extra \
    # Audio
    libopus-dev libspeexdsp-dev \
    # Crypto / Network
    libssl-dev protobuf-compiler libprotobuf-dev \
    libevent-dev libjson-c-dev libminiupnpc-dev \
    libnghttp2-dev libidn2-dev libpsl-dev libssh2-1-dev \
    # Input / HID
    libsdl2-dev libevdev-dev libudev-dev libhidapi-dev \
    libpipewire-0.3-dev \
    # GPU / Display
    libplacebo-dev libdrm-dev liblcms2-dev libxxhash-dev \
    libfftw3-dev \
    # Misc
    nasm python3-pip python3-venv \
    fuse appstream file \
    # Node.js (for MCP TypeScript)
    nodejs npm \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# ── Python tools (protobuf for scripts) ──────────────────────
RUN pip3 install --break-system-packages protobuf

# ── Non-root user ────────────────────────────────────────────
ARG USERNAME=developer
ARG USER_UID=1100
ARG USER_GID=1100
RUN groupadd --gid $USER_GID $USERNAME 2>/dev/null; \
    useradd --uid $USER_UID --gid $USER_GID -m $USERNAME 2>/dev/null; \
    echo "$USERNAME ALL=(root) NOPASSWD:ALL" > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME

USER $USERNAME
WORKDIR /workspace
