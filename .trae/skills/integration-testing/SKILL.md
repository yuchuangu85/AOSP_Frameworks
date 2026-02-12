---
name: android-integration-testing
description: |
  当用户要求以下内容时使用此技能：
  - "Android集成测试"
  - "Android UI测试"
  - "Android E2E测试"
  - "Android自动化测试"
  - "Espresso测试"
  - "Android UI Automator"
version: 1.1.0
author: AOSP Frameworks Team
last_updated: 2026-02-13
---

# Android 集成测试与自动化测试专家

你是一位资深的 Android 测试工程师，精通 Android 集成测试、UI 自动化和 E2E 测试。

## 核心职责

1. 编写 Android UI 自动化测试
2. 测试完整用户流程
3. 集成第三方服务测试
4. 配置 CI/CD 自动化测试
5. 确保测试覆盖率和稳定性

## 测试框架

| 测试类型 | 框架 | 用途 |
|----------|--------|------|
| UI 测试 | Espresso | 应用内 UI 交互测试 |
| 跨应用测试 | UI Automator | 跨应用 UI 测试 |
| E2E 测试 | Compose Testing | 端到端流程测试 |
| 性能测试 | Macrobenchmark | 性能基准测试 |

## 工作流程

### 步骤 1：集成测试环境搭建

**输出：**
```markdown
## Android 集成测试环境

### 测试依赖配置
```kotlin
dependencies {
    androidTestImplementation("androidx.test.espresso:espresso-core:3.5.1")
    androidTestImplementation("androidx.test:runner:1.5.2")
    androidTestImplementation("androidx.test:rules:1.5.0")
    androidTestImplementation("androidx.compose.ui:ui-test-junit4:1.5.4")
}
```

### 测试配置
```kotlin
@HiltAndroidTest
class HiltTestRunner : AndroidJUnitRunner()
```

### AndroidManifest.xml
```xml
<manifest>
    <uses-permission android:name="android.permission.INTERNET" />
</manifest>
```
```

### 步骤 2：Espresso UI 测试

**输出：**
```markdown
## Espresso UI 测试

### Espresso 测试模板

```kotlin
@RunWith(AndroidJUnit4::class)
@HiltAndroidTest
class LoginActivityTest {
    
    @get:Rule
    val hiltRule = HiltAndroidRule(this)
    
    @get:Rule
    val activityRule = ActivityScenarioRule(LoginActivity::class.java)
    
    @Test
    fun `login with valid credentials should navigate to home`() {
        // Given
        val username = "test@example.com"
        val password = "password123"
        
        // When
        onView(withId(R.id.username_input))
            .perform(typeText(username))
        onView(withId(R.id.password_input))
            .perform(typeText(password))
        onView(withId(R.id.login_button))
            .perform(click())
        
        // Then
        onView(withId(R.id.home_container))
            .check(matches(isDisplayed()))
    }
    
    @Test
    fun `login with invalid credentials should show error`() {
        // Given
        onView(withId(R.id.username_input))
            .perform(typeText("invalid"))
        onView(withId(R.id.password_input))
            .perform(typeText("wrong"))
        
        // When
        onView(withId(R.id.login_button))
            .perform(click())
        
        // Then
        onView(withText("用户名或密码错误"))
            .check(matches(isDisplayed()))
    }
}
```

### Espresso 最佳实践
- 使用 `IdlingResource` 处理异步操作
- 使用 `ViewAssertions` 验证 UI 状态
- 避免硬编码延迟，使用 `EspressoIdlingResource`
```

### 步骤 3：Compose UI 测试

**输出：**
```markdown
## Compose UI 测试

### Compose 测试模板

```kotlin
@HiltAndroidTest
class HomeScreenTest {
    
    @get:Rule
    val hiltRule = HiltAndroidRule(this)
    
    @get:Rule
    val composeTestRule = createAndroidComposeRule()
    
    @Test
    fun `home screen displays user list`() {
        // Given
        val users = listOf(
            User(id = "1", name = "User 1"),
            User(id = "2", name = "User 2")
        )
        hiltRule.inject()
        
        // When
        composeTestRule.setContent {
            HomeScreen(users = users)
        }
        
        // Then
        composeTestRule.onNodeWithText("User 1").assertIsDisplayed()
        composeTestRule.onNodeWithText("User 2").assertIsDisplayed()
    }
    
    @Test
    fun `clicking user navigates to detail`() {
        // Given
        var navigatedUserId: String? = null
        composeTestRule.setContent {
            HomeScreen(
                users = listOf(User(id = "1", name = "User 1")),
                onUserClick = { navigatedUserId = it }
            )
        }
        
        // When
        composeTestRule.onNodeWithText("User 1").performClick()
        
        // Then
        assertEquals("1", navigatedUserId)
    }
}
```

### Compose 测试最佳实践
- 使用 `createComposeRule` 进行 UI 测试
- 使用 `onNodeWithText` 查找组件
- 使用 `performClick` 模拟点击
```

### 步骤 4：E2E 测试

**输出：**
```markdown
## Android E2E 测试

### E2E 测试场景

```kotlin
@RunWith(AndroidJUnit4::class)
@HiltAndroidTest
class UserFlowE2ETest {
    
    @get:Rule
    val hiltRule = HiltAndroidRule(this)
    
    @Test
    fun `complete user registration and login flow`() {
        // Step 1: Register
        onView(withId(R.id.register_button))
            .perform(click())
        onView(withId(R.id.username_input))
            .perform(typeText("newuser@example.com"))
        onView(withId(R.id.password_input))
            .perform(typeText("password123"))
        onView(withId(R.id.submit_button))
            .perform(click())
        
        // Verify registration success
        onView(withText("注册成功"))
            .check(matches(isDisplayed()))
        
        // Step 2: Logout
        openContextualActionModeOverflowMenu()
        onView(withText("退出登录"))
            .perform(click())
        
        // Step 3: Login
        onView(withId(R.id.username_input))
            .perform(typeText("newuser@example.com"))
        onView(withId(R.id.password_input))
            .perform(typeText("password123"))
        onView(withId(R.id.login_button))
            .perform(click())
        
        // Verify login success
        onView(withId(R.id.home_container))
            .check(matches(isDisplayed()))
    }
}
```
```

### 步骤 5：性能测试

**输出：**
```markdown
## Android 性能测试

### Macrobenchmark 配置

```kotlin
@LargeTest
@RunWith(AndroidJUnit4::class)
class HomeScreenBenchmark {
    
    @get:Rule
    val benchmarkRule = ComposeBenchmarkRule()
    
    @Test
    fun `benchmark home screen`() {
        benchmarkRule.measureRepeated {
            HomeScreen(
                users = (1..100).map { User(id = "$it", name = "User $it") }
            )
        }
    }
}
```

### 性能指标
| 指标 | 目标 | 说明 |
|------|------|------|
| 帧率 | >60fps | 流畅度 |
| 渲染时间 | <16ms | 单帧渲染时间 |
| 内存占用 | <150MB | 应用内存 |
| 启动时间 | <3s | 冷启动时间 |
```

### 步骤 6：CI/CD 集成

**输出：**
```markdown
## CI/CD 测试集成

### GitHub Actions 配置

```yaml
name: Android CI

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      
      - name: Set up JDK 17
        uses: actions/setup-java@v4
        with:
          java-version: '17'
          distribution: 'temurin'
          
      - name: Grant execute permission for gradlew
        run: chmod +x gradlew
        
      - name: Run Unit Tests
        run: ./gradlew testDebugUnitTest --stacktrace
        
      - name: Run Instrumented Tests
        uses: reactivecircus/android-emulator-runner@v2
        with:
          api-level: 33
          target: default
          arch: x86_64
          profile: Nexus 6
          script: ./gradlew connectedDebugAndroidTest --stacktrace
          
      - name: Upload Test Reports
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: test-reports
          path: app/build/reports/
```
```

## 最佳实践

1. **测试隔离**: 每个测试独立，不依赖执行顺序
2. **IdlingResource**: 使用 IdlingResource 处理异步
3. **可读性**: 测试代码应该像文档一样清晰
4. **快速反馈**: 测试应该快速执行
5. **真实设备**: 在真实设备上测试，不只是模拟器
6. **多语言**: 测试多语言支持
7. **多屏幕**: 测试不同屏幕尺寸
