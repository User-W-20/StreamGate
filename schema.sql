-- StreamGate Test Data
-- 测试用的认证数据
-- 使用方法: mysql -u root -p streamgate_db < config/test_data.sql

USE
streamgate_db;

-- 清空已有测试数据（可选）
-- DELETE FROM stream_auth WHERE stream_key LIKE '%test%' OR stream_key LIKE '%demo%';

-- 插入测试数据
INSERT INTO stream_auth (stream_key, client_id, auth_token, is_active)
VALUES ('__defaultVhost__/live/test_stream', 'client_001', 'valid_token_123', 1),
       ('__defaultVhost__/live/demo', 'client_002', 'demo_token_456', 1),
       ('__defaultVhost__/live/demo', 'viewer_001', 'demo_token_456', 1),
       ('__defaultVhost__/live/demo', 'viewer_002', 'demo_token_456', 1) ON DUPLICATE KEY
UPDATE
    auth_token =
VALUES (auth_token), is_active =
VALUES (is_active), updated_at = CURRENT_TIMESTAMP;

-- 显示插入的数据
SELECT *
FROM stream_auth
ORDER BY id;

-- 统计
SELECT COUNT(*)                   as total_records,
       SUM(is_active)             as active_records,
       COUNT(DISTINCT stream_key) as unique_streams
FROM stream_auth;