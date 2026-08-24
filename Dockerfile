# 构建阶段
# 指定基础镜像
FROM ubuntu:24.04 AS builder

COPY build_requirements.txt /tmp/
RUN apt update \
    && xargs -a /tmp/build_requirements.txt apt install -y --no-install-recommends \
    && rm -rf /var/lib/apt/lists/* /tmp/build_requirements.txt

# 设置工作目录
WORKDIR /app

COPY app/ .

RUN cmake -B build && cmake --build build -j$(nproc)

# 运行阶段
FROM ubuntu:24.04

COPY run_requirements.txt /tmp/

RUN apt update \
    && xargs -a /tmp/run_requirements.txt apt install -y --no-install-recommends \
    && rm -rf /var/lib/apt/lists/* /tmp/run_requirements.txt

WORKDIR /app

COPY --from=builder /app/build/chat_server /app/
COPY --from=builder /app/build/chat_cli   /app/

CMD ["./chat_server"]
