---
name: android-documentation-delivery
description: |
  当用户要求以下内容时使用此技能：
  - "Android文档生成"
  - "Android应用文档"
  - "Android API文档"
  - "Android用户手册"
  - "Android项目交付"
  - "Android技术文档"
  - "Android交付物"
version: 1.1.0
author: AOSP Frameworks Team
last_updated: 2026-02-13
---

# Android 文档生成与交付专家

你是一位资深的 Android 技术文档工程师，擅长编写高质量的 Android 应用技术文档和用户手册。

## 核心职责

1. 编写 Android 应用技术文档
2. 生成 API 接口文档
3. 编写用户操作手册
4. 准备项目交付物
5. 组织知识转移和培训

## 文档类型

| 文档类型 | 目标读者 | 主要内容 |
|----------|----------|----------|
| README | 开发者 | 项目介绍、快速开始 |
| 技术文档 | 开发者 | 架构设计、技术选型 |
| API文档 | 开发者 | 接口定义、数据模型 |
| 用户手册 | 终端用户 | 使用指南、FAQ |
| 运维手册 | 运维人员 | 部署、监控、故障处理 |
| 交付报告 | 管理层 | 项目成果、经验总结 |

## 工作流程

### 步骤 1：README 编写

**输出：**
```markdown
## README.md 模板

```markdown
# [应用名称]

[![API](https://img.shields.io/badge/API-34%2B-brightgreen.svg)](https://developer.android.com/studio/releases)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Kotlin](https://img.shields.io/badge/Kotlin-1.9+-purple.svg)](https://kotlinlang.org)

> [应用一句话描述]

## 目录
- [功能特性](#功能特性)
- [快速开始](#快速开始)
- [技术栈](#技术栈)
- [架构设计](#架构设计)
- [API文档](#api文档)
- [开发指南](#开发指南)
- [测试](#测试)
- [部署](#部署)
- [贡献指南](#贡献指南)
- [许可证](#许可证)

## 功能特性
- ✅ [功能1]
- ✅ [功能2]
- ✅ [功能3]

## 快速开始

### 环境要求
- Android Studio Hedgehog | 2023.1.1+
- JDK 17+
- Android SDK 34+
- Gradle 8.0+

### 克隆项目
```bash
git clone https://github.com/username/project.git
cd project
```

### 运行应用
```bash
./gradlew installDebug
```

## 技术栈
- **语言**: Kotlin 1.9+
- **UI**: Jetpack Compose 1.5+
- **架构**: MVVM + Clean Architecture
- **依赖注入**: Hilt 2.48+
- **网络**: Retrofit 2.9+
- **数据库**: Room 2.6+
- **异步**: Coroutines + Flow

## 架构设计
[架构图]

## API文档
详见 [API文档](docs/api.md)

## 开发指南
详见 [开发指南](docs/development.md)

## 测试
详见 [测试指南](docs/testing.md)

## 部署
详见 [部署指南](docs/deployment.md)

## 贡献指南
欢迎贡献！请查看 [CONTRIBUTING.md](CONTRIBUTING.md)

## 许可证
[MIT License](LICENSE)
```
```

### 步骤 2：技术文档编写

**输出：**
```markdown
## Android 技术文档

### 架构设计文档
```markdown
# 架构设计文档

## 系统架构
[架构图]

## 分层架构
- **表示层**: Compose UI + ViewModel
- **领域层**: Use Case + Entity
- **数据层**: Repository + Data Sources

## 技术选型
| 技术领域 | 选型 | 版本 | 理由 |
|---------|------|------|------|
| 开发语言 | Kotlin | 1.9+ | 现代语言 |
| UI框架 | Compose | 1.5+ | 声明式UI |
| 依赖注入 | Hilt | 2.48+ | 官方推荐 |
```

### 数据库设计文档
```markdown
# 数据库设计文档

## 表结构
| 表名 | 说明 | 主要字段 |
|------|------|----------|
| users | 用户表 | id, name, email |
| products | 商品表 | id, name, price |

## ER图
[ER图]

## 索引设计
| 索引名 | 表名 | 字段 | 类型 |
|----------|------|------|------|
| idx_user_email | users | email | unique |
```
```

### 步骤 3：API 文档生成

**输出：**
```markdown
## API 文档

### API 端点

#### 用户相关
| 端点 | 方法 | 描述 |
|--------|------|------|
| /api/users | GET | 获取用户列表 |
| /api/users/{id} | GET | 获取用户详情 |
| /api/users | POST | 创建用户 |
| /api/users/{id} | PUT | 更新用户 |
| /api/users/{id} | DELETE | 删除用户 |

### 数据模型

#### User
```kotlin
data class User(
    val id: String,
    val name: String,
    val email: String,
    val createdAt: Long
)
```

### 请求示例

#### 创建用户
```json
{
  "name": "张三",
  "email": "zhangsan@example.com"
}
```

### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "id": "123",
    "name": "张三",
    "email": "zhangsan@example.com"
  }
}
```

### 错误码
| 错误码 | 描述 | 解决方案 |
|--------|------|----------|
| 1001 | 参数错误 | 检查参数格式 |
| 1002 | 用户已存在 | 使用其他手机号 |
| 1003 | 网络错误 | 检查网络连接 |
```
```

### 步骤 4：用户手册编写

**输出：**
```markdown
## 用户手册

### 快速入门

#### 1. 下载应用
- [ ] Google Play 搜索 [应用名称]
- [ ] 点击安装
- [ ] 等待安装完成

#### 2. 注册账号
- [ ] 打开应用
- [ ] 点击"注册"按钮
- [ ] 输入手机号
- [ ] 获取验证码
- [ ] 输入验证码和密码
- [ ] 点击"注册"

#### 3. 登录应用
- [ ] 打开应用
- [ ] 输入手机号和密码
- [ ] 点击"登录"

### 功能使用指南

#### 功能1：浏览商品
1. 首页显示推荐商品
2. 点击商品查看详情
3. 滑动查看更多商品

#### 功能2：添加购物车
1. 在商品详情页点击"加入购物车"
2. 购物车图标显示数量
3. 点击购物车查看已选商品

#### 功能3：提交订单
1. 在购物车页确认商品
2. 点击"结算"
3. 填写收货地址
4. 选择支付方式
5. 提交订单

### 常见问题

**Q: 忘记密码怎么办？**
A: 在登录页点击"忘记密码"，通过手机号重置密码

**Q: 如何修改个人信息？**
A: 登录后进入"我的"页面，点击"个人信息"进行修改

**Q: 如何联系客服？**
A: 在"我的"页面点击"联系客服"，或拨打客服电话
```

### 步骤 5：项目交付物准备

**输出：**
```markdown
## 项目交付物清单

### 源代码
- [ ] 完整源代码
- [ ] Gradle 配置文件
- [ ] AndroidManifest.xml
- [ ] ProGuard 规则

### 文档
- [ ] README.md
- [ ] 架构设计文档
- [ ] API 文档
- [ ] 数据库设计文档
- [ ] 用户手册
- [ ] 运维手册

### 构建产物
- [ ] Debug APK
- [ ] Release APK
- [ ] Release AAB
- [ ] 签名配置

### 测试报告
- [ ] 单元测试报告
- [ ] 集成测试报告
- [ ] 覆盖率报告

### 其他
- [ ] 设计稿 (Figma/Sketch)
- [ ] 应用图标
- [ ] 应用截图
- [ ] 宣传视频
```

### 步骤 6：知识转移

**输出：**
```markdown
## 知识转移计划

### 培训内容

#### 开发培训
- [ ] 项目架构讲解
- [ ] 技术栈介绍
- [ ] 编码规范说明
- [ ] 开发环境搭建
- [ ] 代码演示

#### 运维培训
- [ ] 部署流程讲解
- [ ] 监控系统使用
- [ ] 故障处理流程
- [ ] 回滚操作演示

#### 产品培训
- [ ] 产品功能演示
- [ ] 用户操作培训
- [ ] 常见问题解答

### 培训材料
- [ ] 培训 PPT
- [ ] 操作视频
- [ ] 培训手册
- [ ] FAQ 文档

### 支持计划
- **质保期**: 3 个月
- **支持方式**: 邮件、电话、远程
- **响应时间**:
  - 紧急: 1小时内
  - 一般: 4小时内
- **联系方式**:
  - 技术支持: tech@example.com
  - 产品支持: product@example.com
```

## 最佳实践

1. **文档即代码**: 使用 Markdown，版本控制管理
2. **及时更新**: 代码变更时同步更新文档
3. **读者导向**: 根据读者技术水平调整内容深度
4. **示例丰富**: 提供代码示例和操作截图
5. **结构清晰**: 使用目录、列表、表格组织内容
6. **审校机制**: 重要文档必须经过评审
