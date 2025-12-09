---
name: StreamGate
version: 0.1.0
status: active
language: C++
standard: C++20
platform: Linux
---

# StreamGate 

> 基于现代 C++ 的高性能流媒体推流鉴权服务  
> 用于处理高并发 HTTP Hook 请求，提供低延迟授权校验与缓存容错能力。

---

## 🌟 项目简介

**StreamGate Auth Hook Server** 是一个面向流媒体平台的后端控制面服务，  
主要用于 **RTMP / SRT / HTTP-FLV 等推流场景的鉴权校验（Hook 回调）**。

项目关注点：
- 高并发鉴权请求处理
- Redis 缓存加速 + 数据库降级
- 系统稳定性与容错能力

---

## ✨ 核心特性

- ✅ HTTP Hook 推流鉴权接口
- ✅ Redis 缓存优先（Cache-Aside）
- ✅ MySQL / MariaDB 持久化校验
- ✅ Redis / DB 故障自动降级
- ✅ Boost.Asio 异步 I/O + 线程池
- ✅ 清晰的模块分层与抽象设计
- ✅ GoogleTest 覆盖关键故障场景

---

## 🧱 架构设计

#### HTTP Hook Request

#### ↓

#### Boost.Asio HTTP Server

#### ↓

#### AuthManager

#### ↓

#### HybridAuthRepository

#### ↓

#### Redis Cache ──> MySQL / MariaDB

---

## 🔁 请求处理流程

1. 流媒体服务器触发 Hook（如 `on_publish`）
2. HTTP Server 接收并解析 JSON 请求
3. Worker 线程调用 `AuthManager`
4. 查询缓存 / 数据库进行授权校验
5. 返回：
    - ✅ `HTTP 200 OK`（允许推流）
    - ❌ `HTTP 403 Forbidden`（拒绝推流）

---

## 🧩 模块说明

### `AuthManager`
- 核心业务逻辑
- 不直接依赖 Redis / DB 实现

### `HybridAuthRepository`
- 实现缓存 + 数据库访问策略
- 控制 Fail-Open / Fail-Closed 行为

### `CacheManager`
- Redis 访问封装
- 负责缓存序列化与回写

### `DBManager`
- MariaDB / MySQL 访问
- 连接池与错误处理

---

## 🛠️ 技术栈

| 分类 | 技术 |
|----|----|
| 语言 | C++17 / C++20 |
| 网络 / 并发 | Boost.Asio |
| JSON（网络层） | Boost.JSON |
| JSON（数据层） | nlohmann/json |
| 缓存 | Redis (cpp_redis) |
| 数据库 | MariaDB / MySQL |
| 测试 | GoogleTest / GoogleMock |
| 构建 | CMake |
| 平台 | Linux |

---

## ⚙️ 构建与运行

### 环境要求
- GCC / Clang (支持 C++17+)
- CMake ≥ 3.20
- Redis
- MariaDB / MySQL

### 构建
```bash
  git clone https://github.com/User-W-20/StreamGate.git
cd StreamGate
mkdir build && cd build
cmake ..
make
```
### 运行
```
./build/src/streamgate_hook_server 
```
### 🧪 测试
```
sudo ./build/src/test01 
```
### 包含测试场景：
- 正常流程
- Redis 不可用
- DB 不可用
- 双重故障处理

###   🎯 适用场景
- 流媒体推流鉴权（RTMP / SRT / HTTP-FLV）
- 音视频平台控制面后端
- 高并发、低延迟授权服务

###   🚧 当前状态
#### 已完成：
- 核心鉴权逻辑
- 缓存 + DB 降级
- 并发处理模型
- 核心测试覆盖

####   规划中：
- 推拉流任务调度

- FFmpeg probe / 推流最小集成

- HTTP 管理接口

- 监控与运行指标