# Dockerfile

# 使用一个与麒麟系统相似的、基于 aarch64 架构的 Ubuntu LTS 镜像
# 我们明确指定了架构 arm64/ubuntu:20.04
FROM --platform=linux/arm64 ubuntu:20.04

# 设置环境变量，避免安装过程中的交互式提问
ENV DEBIAN_FRONTEND=noninteractive

# 更新软件源并安装编译所需的所有依赖
RUN apt-get update && \
    apt-get install -y build-essential libgtk-3-dev libudisks2-dev pkg-config

# 将我们的源代码复制到容器的 /app 目录下
WORKDIR /app
COPY main.c .

# 在容器内执行编译命令，生成名为 cd_manager_c 的可执行文件
RUN gcc main.c -o cd_manager $(pkg-config --cflags --libs gtk+-3.0 udisks2)
