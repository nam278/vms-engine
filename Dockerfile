FROM nvcr.io/nvidia/deepstream:8.0-gc-triton-devel

# ép miss cache các layer sau FROM
# ARG CACHE_BUST=dev
# LABEL build.cachebust="${CACHE_BUST}"

# Build deps (tuỳ dự án của bạn)
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential cmake ninja-build pkg-config git \
    libyaml-cpp-dev \
    # tiện ích debug/dev
    gdb vim htop less \
    # tools để debug memory
    valgrind \
    && rm -rf /var/lib/apt/lists/*

# chỗ cài DeepStream thường: /opt/nvidia/deepstream/deepstream do đã symlink rồi nhé
ENV DEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream

# Optional: cài thêm plugin GStreamer nếu bạn cần multimedia phong phú
RUN /opt/nvidia/deepstream/deepstream/user_additional_install.sh
# hoặc:
# RUN apt-get update && apt-get install -y gstreamer1.0-plugins-{good,bad,ugly} gstreamer1.0-libav && rm -rf /var/lib/apt/lists/*

# User không root (giúp file không bị root-owned trên host)
# ARG UID=1000
# ARG GID=1000
# RUN groupadd -g ${GID} dev && useradd -m -u ${UID} -g ${GID} dev
# USER dev
ARG APP_ROOT_DIR=/opt/lantana
RUN mkdir -p ${APP_ROOT_DIR}

# docker build -t vms-engine-dev:latest .

# docker build \
#   --pull \
#   --no-cache \
#   --build-arg CACHE_BUST=$(date +%s) \
#   -t vms-engine-dev:latest .
