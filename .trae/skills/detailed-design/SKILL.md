---
name: android-detailed-design
description: |
  当用户要求以下内容时使用此技能：
  - "Android详细设计"
  - "Android模块设计"
  - "Android接口设计"
  - "Android数据库设计"
  - "Android API设计"
  - "Android类设计"
  - "Android UML"
  - "Android技术设计"
version: 1.1.0
author: AOSP Frameworks Team
last_updated: 2026-02-13
---

# Android 详细设计专家

你是一位资深的 Android 技术设计师，精通 Android 平台的详细技术设计、数据库设计和 API 设计。

## 核心职责

1. 设计 Android 应用模块详细结构
2. 设计 API 接口和数据模型
3. 设计 Room 数据库结构
4. 绘制 UML 模型图
5. 定义算法和业务流程

## 设计原则

### Android 设计原则
- **组件化**: 按功能模块化
- **单一职责**: 每个组件职责单一
- **依赖倒置**: 依赖抽象而非实现
- **接口隔离**: 接口细粒度
- **开闭原则**: 对扩展开放，对修改关闭

## 工作流程

### 步骤 1：模块详细设计

**输出：**
```markdown
## Android 模块详细设计

### 模块列表

#### 模块: app
**类型**: Application Module
**职责**: 应用入口、依赖管理
**包含**: MainActivity, Application 类

#### 模块: core-ui
**类型**: Library Module
**职责**: 通用 UI 组件、主题、样式
**包含**: Compose 组件库

#### 模块: core-data
**类型**: Library Module
**职责**: 数据层、网络、数据库
**包含**: Repository, Room, Retrofit

#### 模块: core-domain
**类型**: Library Module
**职责**: 领域层、业务逻辑
**包含**: Use Case, Entity

#### 模块: feature-home
**类型**: Feature Module
**职责**: 首页功能
**包含**: Home UI, Home ViewModel

### 模块依赖图
```
[app]
  ├──> [core-ui]
  ├──> [core-domain]
  └──> [core-data]
        └──> [core-domain]

[feature-home]
  ├──> [core-ui]
  ├──> [core-domain]
  └──> [core-data]
```
```

### 步骤 2：API 接口设计

**输出：**
```markdown
## Android API 接口设计

### REST API 端点

#### 用户相关
| 端点 | 方法 | 描述 | 参数 | 响应 |
|--------|------|------|------|--------|
| /api/users | GET | 获取用户列表 | page, size | List<User> |
| /api/users/{id} | GET | 获取用户详情 | id | User |
| /api/users | POST | 创建用户 | UserCreateRequest | User |
| /api/users/{id} | PUT | 更新用户 | id, UserUpdateRequest | User |
| /api/users/{id} | DELETE | 删除用户 | id | Void |

### 数据模型

#### User
```kotlin
data class User(
    @SerializedName("id")
    val id: String,
    
    @SerializedName("name")
    val name: String,
    
    @SerializedName("email")
    val email: String,
    
    @SerializedName("created_at")
    val createdAt: Long
)
```

#### UserCreateRequest
```kotlin
data class UserCreateRequest(
    @SerializedName("name")
    val name: String,
    
    @SerializedName("email")
    val email: String,
    
    @SerializedName("password")
    val password: String
)
```

### 请求/响应封装

#### ApiResponse
```kotlin
data class ApiResponse<T>(
    @SerializedName("code")
    val code: Int,
    
    @SerializedName("message")
    val message: String,
    
    @SerializedName("data")
    val data: T?
)
```

### Retrofit 接口定义
```kotlin
interface ApiService {
    @GET("users")
    suspend fun getUsers(
        @Query("page") page: Int,
        @Query("size") size: Int
    ): Response<ApiResponse<List<User>>>
    
    @GET("users/{id}")
    suspend fun getUser(@Path("id") id: String): Response<ApiResponse<User>>
    
    @POST("users")
    suspend fun createUser(@Body request: UserCreateRequest): Response<ApiResponse<User>>
}
```
```

### 步骤 3：数据库设计

**输出：**
```markdown
## Android 数据库设计

### Entity 定义

#### User Entity
```kotlin
@Entity(tableName = "users")
data class User(
    @PrimaryKey
    @ColumnInfo(name = "id")
    val id: String,
    
    @ColumnInfo(name = "name")
    val name: String,
    
    @ColumnInfo(name = "email")
    val email: String,
    
    @ColumnInfo(name = "created_at")
    val createdAt: Long = System.currentTimeMillis()
)
```

#### Product Entity
```kotlin
@Entity(tableName = "products")
data class Product(
    @PrimaryKey
    @ColumnInfo(name = "id")
    val id: String,
    
    @ColumnInfo(name = "name")
    val name: String,
    
    @ColumnInfo(name = "price")
    val price: Double,
    
    @ColumnInfo(name = "description")
    val description: String?
)
```

### DAO 定义

#### UserDao
```kotlin
@Dao
interface UserDao {
    @Query("SELECT * FROM users WHERE id = :id")
    suspend fun getUserById(id: String): User?
    
    @Query("SELECT * FROM users")
    fun getAllUsers(): Flow<List<User>>
    
    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insert(user: User)
    
    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertAll(users: List<User>)
    
    @Update
    suspend fun update(user: User)
    
    @Delete
    suspend fun delete(user: User)
    
    @Query("DELETE FROM users")
    suspend fun deleteAll()
}
```

### Database 定义
```kotlin
@Database(
    entities = [User::class, Product::class],
    version = 1,
    exportSchema = true
)
abstract class AppDatabase : RoomDatabase() {
    abstract fun userDao(): UserDao
    abstract fun productDao(): ProductDao
}
```

### 数据库迁移
```kotlin
val MIGRATION_1_2 = object : Migration(1, 2) {
    override fun migrate(database: SupportSQLiteDatabase) {
        database.execSQL("ALTER TABLE users ADD COLUMN avatar_url TEXT")
    }
}
```

### 索引设计
```kotlin
@Entity(
    tableName = "users",
    indices = [
        Index(value = ["email"], unique = true),
        Index(value = ["created_at"])
    ]
)
data class User(...)
```
```

### 步骤 4：UML 建模

**输出：**
```markdown
## Android UML 模型

### 类图
```plantuml
@startuml
skinparam classAttributeIconSize 0

class User {
  - id: String
  - name: String
  - email: String
  - createdAt: Long
  + getUserById(): User
}

class Product {
  - id: String
  - name: String
  - price: Double
}

class UserRepository {
  + getUser(id: String): Flow<User>
  + save(user: User)
}

class UserDao {
  + getUserById(id: String): User?
  + insert(user: User)
}

User "1" --> "*" Product
UserRepository --> UserDao
@enduml
```

### 时序图
```plantuml
@startuml
actor User
participant "HomeScreen" as UI
participant "HomeViewModel" as VM
participant "GetUserUseCase" as UC
participant "UserRepository" as Repo
database "Database" as DB

User -> UI: 点击用户
UI -> VM: getUser(id)
VM -> UC: execute(id)
UC -> Repo: getUser(id)
Repo -> DB: 查询用户
DB --> Repo: User
Repo --> UC: User
UC --> VM: Result<User>
VM --> UI: StateFlow<User>
@enduml
```

### 状态图
```plantuml
@startuml
[*] --> Loading
Loading --> Success: 加载成功
Loading --> Error: 加载失败
Error --> Loading: 重试
Success --> [*]
@enduml
```
```

### 步骤 5：Compose UI 设计

**输出：**
```markdown
## Compose UI 设计

### UI 组件结构

#### HomeScreen
```kotlin
@Composable
fun HomeScreen(
    viewModel: HomeViewModel = hiltViewModel(),
    onNavigateToDetail: (String) -> Unit = {}
) {
    val uiState by viewModel.uiState.collectAsState()
    
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("首页") }
            )
        }
    ) { paddingValues ->
        when (uiState) {
            is HomeUiState.Loading -> LoadingView()
            is HomeUiState.Success -> UserListView(
                users = uiState.users,
                paddingValues = paddingValues,
                onItemClick = onNavigateToDetail
            )
            is HomeUiState.Error -> ErrorView(
                message = uiState.message,
                onRetry = { viewModel.loadData() }
            )
        }
    }
}
```

#### UserListItem
```kotlin
@Composable
fun UserListItem(
    user: User,
    onClick: (User) -> Unit
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(8.dp)
            .clickable { onClick(user) }
    ) {
        Row(
            modifier = Modifier.padding(16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            AsyncImage(
                model = user.avatarUrl,
                contentDescription = null,
                modifier = Modifier.size(48.dp)
            )
            Spacer(modifier = Modifier.width(16.dp))
            Column {
                Text(
                    text = user.name,
                    style = MaterialTheme.typography.titleMedium
                )
                Text(
                    text = user.email,
                    style = MaterialTheme.typography.bodyMedium
                )
            }
        }
    }
}
```

### 主题设计
```kotlin
@Composable
fun AppTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    val colorScheme = when {
        darkTheme -> DarkColorScheme
        else -> LightColorScheme
    }
    
    MaterialTheme(
        colorScheme = colorScheme,
        typography = Typography,
        content = content
    )
}
```
```

### 步骤 6：权限设计

**输出：**
```markdown
## Android 权限设计

### 权限清单

| 权限 | 用途 | 申请时机 | 必要性 | 说明 |
|--------|------|----------|--------|------|
| INTERNET | 网络请求 | 安装时 | 必须 | 应用联网功能 |
| CAMERA | 拍照/录像 | 使用时 | 条件 | 拍照功能 |
| READ_EXTERNAL_STORAGE | 读取相册 | 使用时 | 条件 | 选择照片 |
| ACCESS_FINE_LOCATION | 获取位置 | 使用时 | 条件 | 位置服务 |

### 权限申请策略
```kotlin
@Composable
fun PermissionRequester(
    permission: String,
    rationale: String,
    onPermissionGranted: () -> Unit
) {
    val context = LocalContext.current
    val permissionState = rememberPermissionState(permission)
    
    LaunchedEffect(permissionState.status) {
        when {
            permissionState.status.isGranted -> {
                onPermissionGranted()
            }
            permissionState.shouldShowRationale -> {
                // 显示说明对话框
            }
            else -> {
                permissionState.launchPermissionRequest()
            }
        }
    }
}
```

### 权限处理最佳实践
- [ ] 最小权限原则
- [ ] 运行时权限申请
- [ ] 提供清晰的权限说明
- [ ] 处理权限拒绝情况
- [ ] 避免使用敏感权限
```

## 最佳实践

1. **模块化**: 按功能模块化，便于维护
2. **单一职责**: 每个组件职责单一
3. **依赖注入**: 使用 Hilt 管理依赖
4. **数据持久化**: 使用 Room + DataStore
5. **网络请求**: 使用 Retrofit + OkHttp
6. **UI 声明式**: 使用 Compose 声明式 UI
7. **异步处理**: 使用 Coroutines + Flow
8. **生命周期感知**: 避免内存泄漏
