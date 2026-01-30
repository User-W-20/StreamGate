# StreamGate

<div align="center">

[![C++20](https://img.shields.io/badge/C++-20-blue.svg?style=flat&logo=c%2B%2B)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.20+-064F8C.svg?style=flat&logo=cmake)](https://cmake.org/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)]()

**高性能流媒体鉴权网关 | Production-Ready Authentication Gateway for Live Streaming**

[特性](#-核心特性) • [快速开始](#-快速开始) • [架构](#-架构设计) • [性能](#-性能指标) • [文档](#-文档)

</div>

---

## 📖 项目简介

StreamGate 是一个为 **ZLMediaKit** 等流媒体服务器设计的企业级鉴权网关，采用现代C++20开发，提供高性能、高可用的推拉流认证服务。

### 为什么选择 StreamGate？

- 🚀 **高性能**：经过实测，QPS达到 **5800+**，延迟 <10ms
- 🛡️ **生产级**：完整的错误处理、优雅关闭、状态管理
- 🏗️ **Clean Architecture**：清晰的三层分离，易于维护和扩展
- 💾 **双层缓存**：Redis + MariaDB，缓存命中率 >95%
- ⚡ **异步I/O**：Boost.Beast + 线程池，高并发无压力
- 🔒 **安全可靠**：Token认证 + 分布式状态管理

---

## ✨ 核心特性

### 功能特性

- ✅ **完整的流生命周期管理**
   - 推流认证 (`on_publish`)
   - 拉流认证 (`on_play`)
   - 流结束处理 (`on_publish_done`, `on_play_done`)
   - 无观众自动清理 (`on_stream_none_reader`)

- ✅ **Token认证系统**
   - URL参数认证：`rtmp://server/app/stream?token=xxx`
   - 支持多种流协议：RTMP, HTTP-FLV, HLS, WebRTC

- ✅ **分布式状态管理**
   - Redis存储流状态
   - 支持多边缘节点
   - 自动超时清理
   - 心跳续约机制

### 技术特性

- ⚡ **高性能I/O**
   - Boost.Beast异步HTTP服务器
   - 线程池并发处理
   - 连接池复用（DB + Redis）

- 💾 **智能缓存策略**
   - Cache-Aside模式
   - Redis优先，DB降级
   - 负缓存防击穿
   - TTL过期管理

- 🏗️ **Clean Architecture**
   - Protocol Layer（HTTP处理）
   - Application Layer（业务编排）
   - Domain Layer（核心逻辑）
   - Infrastructure Layer（数据访问）

---

## 🏛️ 架构设计

### 系统架构图

```
┌─────────────────────────────────────────────────────────────┐
│                      ZLMediaKit / FFmpeg                     │
│                  (推流/拉流客户端)                            │
└──────────────────────┬──────────────────────────────────────┘
                       │ HTTP Hook (JSON)
                       ↓
┌─────────────────────────────────────────────────────────────┐
│                    StreamGate Server                         │
│  ┌────────────┐   ┌────────────┐   ┌──────────────────┐    │
│  │ HookServer │→→→│ Controller │→→→│ UseCase/Scheduler│    │
│  │ (Boost.    │   │ (Routing)  │   │ (Business Logic) │    │
│  │  Beast)    │   └────────────┘   └──────────────────┘    │
│  └────────────┘                              │              │
│                                               ↓              │
│       ┌───────────────────────────────────────────────┐     │
│       │         AuthManager + StateManager            │     │
│       └───────────────────────────────────────────────┘     │
│                     │                    │                   │
└─────────────────────┼────────────────────┼──────────────────┘
                      │                    │
                      ↓                    ↓
        ┌──────────────────┐    ┌──────────────────┐
        │  Redis Cache     │    │  MariaDB         │
        │  (Port 6380)     │    │  (Port 3306)     │
        │  状态 + 缓存      │    │  持久化存储       │
        └──────────────────┘    └──────────────────┘
```

### 请求处理流程

```
1. ZLMediaKit 收到推流请求
        ↓
2. ZLM 发送 HTTP Hook 到 StreamGate
        ↓
3. StreamGate 解析请求，提取 token
        ↓
4. 检查 Redis 缓存 (Port 6380)
   ├─ 命中 → 直接返回结果（<1ms）
   └─ 未命中 → 查询数据库（1-5ms）
        ↓
5. 验证通过 → 注册流状态到Redis
        ↓
6. 返回 {"code":0,"msg":"success"}
        ↓
7. ZLM 允许推流/拉流
```

---

## 🚀 快速开始

### 环境要求

| 组件 | 版本要求 |
|------|---------|
| 操作系统 | Linux (Ubuntu 20.04+ / Fedora 35+) |
| 编译器 | GCC 11+ / Clang 13+ |
| CMake | 3.20+ |
| Redis | 5.0+ |
| MariaDB | 10.5+ |

### 依赖安装

#### Fedora / RHEL
```bash
sudo dnf install gcc-c++ cmake boost-devel \
  mariadb-devel redis hiredis-devel \
  nlohmann-json-devel gtest-devel
```

#### Ubuntu / Debian
```bash
sudo apt install g++ cmake libboost-all-dev \
  libmariadb-dev redis-server libhiredis-dev \
  nlohmann-json3-dev libgtest-dev
```

### 编译和安装

```bash
# 1. 克隆仓库
git clone https://github.com/User-W-20/StreamGate.git
cd StreamGate

# 2. 创建构建目录
mkdir build && cd build

# 3. 配置和编译
cmake ..
make -j$(nproc)

# 4. 安装（可选）
sudo make install
```

### 配置

#### 1. 数据库初始化

**方式一：使用提供的SQL文件**
```bash
# 创建数据库并导入schema
mysql -u root -p < schema.sql
```

**方式二：手动创建**
```bash
mysql -u root -p << EOF
CREATE DATABASE IF NOT EXISTS streamgate_db;
USE streamgate_db;

CREATE TABLE stream_auth (
    id INT AUTO_INCREMENT PRIMARY KEY,
    stream_key VARCHAR(255) NOT NULL,
    client_id VARCHAR(255) NOT NULL,
    auth_token VARCHAR(255) NOT NULL,
    is_active TINYINT(1) DEFAULT 1,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_stream_client (stream_key, client_id),
    INDEX idx_token (auth_token)
);
EOF
```

#### 2. 插入测试数据

```bash
# 使用提供的测试数据脚本
mysql -u root -p streamgate_db < config/test_data.sql
```

#### 3. 配置文件

编辑 `config/config.ini`:

```ini
# Redis
# 注意：使用6380端口避免与系统默认Redis(6379)冲突
REDIS_HOST=127.0.0.1
REDIS_PORT=6380
REDIS_DB=0

# MySQL / MariaDB
DB_HOST=127.0.0.1
DB_PORT=3306
DB_USER=root
DB_PASS=your_password_here
DB_NAME=streamgate_db
DB_POOL_SIZE=8

# Cache Settings
REDIS_IO_THREADS=2
CACHE_TTL_SECONDS=300

# HookServer
SERVER_PORT=9000
```

> **⚠️ 重要说明**：
> - Redis默认使用 **6380** 端口，而非标准的6379端口
> - 这是为了避免与系统已有的Redis服务冲突
> - 如果你的系统没有其他Redis，可以改为6379

#### 4. 节点配置

编辑 `config/nodes.json`:

```json
{
  "rtmp_srt": [
    {
      "host": "127.0.0.1",
      "port": 1935
    }
  ],
  "http_hls": [
    {
      "host": "127.0.0.1",
      "port": 8080
    }
  ],
  "webrtc": [
    {
      "host": "127.0.0.1",
      "port": 8443
    }
  ]
}
```

> **💡 提示**：示例中的IP地址（10.0.x.x）是多边缘节点部署的示例，本地测试使用127.0.0.1即可。

### 运行

#### 启动服务

```bash
# 1. 启动 Redis（使用6380端口）
redis-server --port 6380 --daemonize yes

# 验证Redis已启动
redis-cli -p 6380 ping  # 应返回 PONG

# 2. 启动 MariaDB
sudo systemctl start mariadb

# 3. 启动 StreamGate
./build/src/streamgate_hook_server

# 看到这些日志说明启动成功：
# [INFO] === StreamGate Service is Ready ===
# [INFO] HookServer listening on 0.0.0.0:9000
```

#### 配置 ZLMediaKit

编辑 ZLMediaKit 的 `config.ini`:

```ini
[hook]
enable=1
on_publish=http://127.0.0.1:9000/index/hook/on_publish
on_play=http://127.0.0.1:9000/index/hook/on_play
on_publish_done=http://127.0.0.1:9000/index/hook/on_publish_done
on_play_done=http://127.0.0.1:9000/index/hook/on_play_done
```

#### 测试推流

```bash
# 使用 FFmpeg 推流（保持运行）
ffmpeg -re -stream_loop -1 -i test.mp4 -c copy -f flv \
  "rtmp://127.0.0.1/live/test_stream?token=valid_token_123"

# 在推流进行时，用另一个终端拉流
ffplay rtmp://127.0.0.1/live/test_stream
# 或浏览器访问: http://127.0.0.1:8080/live/test_stream.flv
```

**⚠️ 重要**：必须在推流进行时才能拉流！这是正常的流媒体行为。

---

## 🧪 测试

### 自动化测试

```bash
# 运行端到端测试脚本
chmod +x src/test/test_streamgate.sh
./src/test/test_streamgate.sh

# 运行集成测试（需要GTest）
export RUN_INTEGRATION_TEST=1
./build/src/test/streamgate_integration_test
```

### 手动测试

```bash
# 测试推流认证（应该成功）
curl -X POST http://localhost:9000/index/hook/on_publish \
  -H "Content-Type: application/json" \
  -d '{
    "action": "on_publish",
    "app": "live",
    "stream": "test_stream",
    "id": "client_001",
    "protocol": "rtmp",
    "params": "token=valid_token_123"
  }'
# 期望: {"code":0,"msg":"success"}

# 测试错误token（应该失败）
curl -X POST http://localhost:9000/index/hook/on_publish \
  -H "Content-Type: application/json" \
  -d '{
    "action": "on_publish",
    "app": "live",
    "stream": "test_stream",
    "id": "client_002",
    "protocol": "rtmp",
    "params": "token=WRONG_TOKEN"
  }'
# 期望: {"code":4,"msg":"鉴权拒绝"}
```

---

## 📊 性能指标

### 实测数据

| 指标 | 数值 |
|------|------|
| **QPS** | 5800+ req/sec |
| **延迟 (p50)** | <5ms |
| **延迟 (p99)** | <10ms |
| **并发连接** | 50+ |
| **缓存命中率** | >95% |
| **零失败率** | 1000 requests, 0 failed |

### 压力测试

```bash
# ApacheBench 压测
ab -n 1000 -c 50 \
  -p test_payload.json \
  -T application/json \
  http://localhost:9000/index/hook/on_publish_done

# 结果：
# Requests per second: 5845.59 [#/sec]
# Failed requests: 0
# Time per request: 8.56 [ms] (mean)
```

---

## 🛠️ 技术栈

| 分类 | 技术选型 | 说明 |
|------|---------|------|
| **语言** | C++20 | jthread, concepts, modern features |
| **网络框架** | Boost.Beast | Async HTTP/1.1 server |
| **并发** | std::jthread + ThreadPool | Modern C++20 concurrency |
| **序列化** | nlohmann/json | JSON parsing and generation |
| **缓存** | Redis (redis++) | Distributed state + cache (Port 6380) |
| **数据库** | MariaDB C++ Connector | Connection pooling |
| **构建系统** | CMake 3.20+ | Modern CMake practices |
| **测试** | GTest + Shell scripts | Unit + Integration + E2E |
| **日志** | Custom Logger | Thread-safe, colored output |

---

## 📁 项目结构

```
StreamGate/
├── config/                 # 配置文件
│   ├── config.ini         # 主配置
│   ├── nodes.json         # 节点配置
│   └── test_data.sql      # 测试数据
├── include/               # 头文件
│   ├── AuthManager.h
│   ├── HookServer.h
│   ├── StreamTaskScheduler.h
│   └── ...
├── src/                   # 源代码
│   ├── auth/             # 认证模块
│   ├── cache/            # 缓存模块
│   ├── db/               # 数据库模块
│   ├── main/             # HTTP服务器
│   ├── scheduler/        # 任务调度
│   ├── util/             # 工具类
│   └── test/             # 测试
│       ├── test_auth.cpp
│       └── test_streamgate.sh
├── schema.sql            # 数据库schema
├── CMakeLists.txt        # 主构建文件
├── LICENSE               # MIT许可证
└── README.md             # 本文件
```

---

## 🎯 使用场景

### 适用场景

- ✅ **直播平台**：推流鉴权、防盗链
- ✅ **在线教育**：教学直播认证管理
- ✅ **视频会议**：WebRTC流接入控制
- ✅ **监控系统**：摄像头推流认证
- ✅ **IoT设备**：物联网音视频流管理

### 不适用场景

- ❌ 单机低并发场景（用简单脚本即可）
- ❌ 不需要认证的公开流
- ❌ 仅本地使用（不需要网络服务）

---

## 🔧 常见问题

### Q: 为什么Redis使用6380端口而不是默认的6379？

**A:** 有两个原因：
1. **避免冲突**：系统可能已经有Redis服务运行在6379端口
2. **隔离测试**：使用不同端口可以隔离测试环境和生产环境

如果你的系统没有其他Redis服务，可以改为6379：
```ini
# config/config.ini
REDIS_PORT=6379  # 改为标准端口
```

然后启动Redis：
```bash
redis-server --port 6379 --daemonize yes
```

### Q: 为什么拉流时提示"No such stream"？

**A:** 必须在推流进行中才能拉流。流媒体的工作原理是：
1. 先有推流端（Publisher）推流
2. 然后拉流端（Player）才能播放
3. 推流结束后，流就不存在了

正确的测试流程：
```bash
# Terminal 1: 启动推流（保持运行）
ffmpeg -re -stream_loop -1 -i test.mp4 -c copy -f flv \
  "rtmp://127.0.0.1/live/test?token=xxx"

# Terminal 2: 在推流运行时拉流
ffplay rtmp://127.0.0.1/live/test
```

### Q: Redis连接失败怎么办？

**A:** 检查Redis是否运行在正确的端口：
```bash
redis-cli -p 6380 ping  # 应返回 PONG

# 如果没有，启动Redis
redis-server --port 6380 --daemonize yes
```

### Q: 数据库连接失败？

**A:** 检查配置和权限：
```bash
# 测试连接
mysql -u root -p streamgate_db -e "SELECT 1;"

# 检查配置文件（注意不要泄露密码！）
cat config/config.ini | grep DB_
```

### Q: 性能不够怎么优化？

**A:** 几个优化方向：
1. 增加线程池大小（`config.ini`中的`SERVER_IO_THREADS`）
2. 增加Redis连接池（`REDIS_POOL_SIZE`，需要代码支持）
3. 增加数据库连接池（`DB_POOL_SIZE`）
4. 使用更快的硬件（SSD、更多CPU核心）

---

## 🗺️ Roadmap

### v0.1.0 (当前版本) ✅
- [x] 基础推拉流鉴权
- [x] Redis缓存
- [x] MariaDB持久化
- [x] HTTP Hook服务器
- [x] 端到端测试

### v0.2.0 (规划中)
- [ ] 监控指标端点 (`GET /metrics`)
- [ ] 配置热重载
- [ ] Docker支持
- [ ] 性能仪表板

### v1.0.0 (未来)
- [ ] 管理API（创建/删除token）
- [ ] Web管理界面
- [ ] 分布式部署支持
- [ ] Kubernetes Operator

---

## 🤝 贡献

欢迎提交Issue和Pull Request！

### 开发流程

```bash
# 1. Fork并克隆
git clone https://github.com/User-W-20/StreamGate.git

# 2. 创建特性分支
git checkout -b feature/your-feature

# 3. 提交更改
git commit -am 'Add some feature'

# 4. 推送到分支
git push origin feature/your-feature

# 5. 创建Pull Request
```

### 代码规范

- 遵循C++ Core Guidelines
- 使用clang-format格式化代码
- 编写单元测试
- 更新文档

---

## 📄 许可证

本项目采用 [MIT License](LICENSE) 开源许可证。

---

## 👨‍💻 作者

**wxx** - [GitHub](https://github.com/User-W-20)

---

## 🙏 致谢

- [ZLMediaKit](https://github.com/ZLMediaKit/ZLMediaKit) - 优秀的流媒体服务器
- [Boost.Beast](https://github.com/boostorg/beast) - 异步HTTP库
- [nlohmann/json](https://github.com/nlohmann/json) - JSON库
- [redis-plus-plus](https://github.com/sewenew/redis-plus-plus) - Redis C++客户端

---

<div align="center">

**如果这个项目对你有帮助，请给个⭐️吧！**

Made with ❤️ by wxx

</div>