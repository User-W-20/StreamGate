# 使用 Ubuntu 24.04 基础镜像
FROM ubuntu:24.04

# 安装编译工具和依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    libssl-dev \
    pkg-config \
    libpoco-dev \
    redis-tools \
    ffmpeg \
    curl \
    && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /app

# 复制项目源码
COPY . .

# 创建编译目录并编译
RUN mkdir -p build && cd build \
    && cmake .. \
    && make -j$(nproc)

# 暴露 HookServer 端口
EXPOSE 8080

# 启动 HookServer
CMD ["./build/src/streamgate_hook_server"]
