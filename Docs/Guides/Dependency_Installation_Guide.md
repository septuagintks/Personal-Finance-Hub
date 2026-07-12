# Personal Finance Hub - 依赖安装指南

Version: 1.1
Updated: 2026-07-13

---

## 当前依赖列表

### 已接入依赖

| 依赖              | 用途         | 状态    |
| ----------------- | ------------ | ------- |
| **spdlog**        | 日志库       | ✅ 必需 |
| **nlohmann-json** | JSON 解析    | ✅ 必需 |
| **GTest**         | 单元测试框架 | ✅ 必需 |

### 编译与运行环境依赖

| 依赖 | 用途 | 状态 |
| --- | --- | --- |
| 支持 `std::chrono` IANA tzdb 的 C++23 标准库 | Dashboard 用户时区月窗 | 必需，以 CMake 能力探测为准 |
| `tzdata` | Linux IANA 时区数据库 | Linux 编译、测试和运行环境必需 |

### P1-S10 必需依赖

| 依赖       | 用途              | 状态           |
| ---------- | ----------------- | -------------- |
| **Drogon** | Web 框架          | P1-S10 接入 |
| **libpq**  | PostgreSQL 客户端 | P1-S10 接入 |

---

## 安装方式

### 方式 1: 使用 vcpkg (推荐)

#### 1.1 安装 vcpkg

如果还没有安装 vcpkg：

```powershell
# 克隆 vcpkg
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg

# 运行 bootstrap
.\bootstrap-vcpkg.bat

# 添加到 PATH（可选）
# 或者记住 vcpkg.exe 的路径
```

#### 1.2 使用 vcpkg.json 自动安装

项目根目录已包含 `vcpkg.json` 清单文件，使用以下命令自动安装所有依赖：

```powershell
# 方式 A: 使用 vcpkg 集成
vcpkg integrate install

# 方式 B: 使用 CMake 工具链文件
cmake -B build -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake
```

#### 1.3 手动安装各个依赖

```powershell
vcpkg install spdlog
vcpkg install nlohmann-json
vcpkg install gtest
```

**Windows 平台指定**:

```powershell
vcpkg install spdlog:x64-windows
vcpkg install nlohmann-json:x64-windows
vcpkg install gtest:x64-windows
```

---

### 方式 2: 系统包管理器

#### Windows (Chocolatey)

```powershell
# 注意：可能版本不是最新
choco install spdlog
# nlohmann-json 和 gtest 可能需要手动编译
```

#### Linux (apt)

```bash
sudo apt-get update
sudo apt-get install -y \
  gcc-14 g++-14 \
  tzdata \
  libspdlog-dev \
  nlohmann-json3-dev \
  libgtest-dev
```

发行版中的具体 GCC 包名可能不同。版本号不是唯一判定条件：CMake configure
会编译并链接 `std::chrono::locate_zone` 探针，未通过时会直接停止。Clang 在
Linux 上通常仍使用系统 `libstdc++`，因此只升级 Clang 不一定具备 tzdb 支持。

#### macOS (Homebrew)

```bash
brew install spdlog
brew install nlohmann-json
brew install googletest
```

---

### 方式 3: 手动编译

如果无法使用包管理器，可以手动下载和编译各个库。

参考各个库的官方文档：

- spdlog: https://github.com/gabime/spdlog
- nlohmann-json: https://github.com/nlohmann/json
- GoogleTest: https://github.com/google/googletest

---

## 配置 CMake

### 使用 vcpkg 工具链

```powershell
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Debug
```

### 使用系统库

如果依赖已安装到系统路径，CMake 应该能自动找到：

```powershell
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
```

### 指定自定义路径

如果库安装在非标准位置：

```powershell
cmake .. -DCMAKE_PREFIX_PATH=/path/to/libs -DCMAKE_BUILD_TYPE=Debug
```

---

## 验证安装

### 构建项目

```powershell
cd build
cmake --build . --config Debug
```

若 configure 报 `PFH requires a C++ standard library with std::chrono timezone
database support`，请升级编译器及其配套标准库，并确认 Linux 已安装 `tzdata`。

### 运行单元测试

```powershell
ctest -C Debug --output-on-failure
```

### 运行主程序

```powershell
./pfh_server.exe
```

---

## 常见问题

### Q: 找不到 spdlog

**A**: 确保使用了正确的 CMake 工具链文件，或者 spdlog 已安装到系统路径。

```powershell
# 检查 vcpkg 安装状态
vcpkg list

# 重新安装
vcpkg remove spdlog
vcpkg install spdlog
```

### Q: GTest 找不到

**A**: 确保 GTest 已正确安装并可被 CMake 找到。

```powershell
vcpkg install gtest
```

### Q: nlohmann-json 链接错误

**A**: nlohmann-json 是 header-only 库，不需要链接。确保包含路径正确。

```cmake
target_link_libraries(target PRIVATE nlohmann_json::nlohmann_json)
```

### Q: Windows 上找不到依赖

**A**: 确保指定了正确的三元组（triplet）：

```powershell
vcpkg install spdlog:x64-windows
vcpkg install nlohmann-json:x64-windows
vcpkg install gtest:x64-windows
```

---

## 依赖版本

当前使用的依赖版本（vcpkg 默认）：

- **spdlog**: 1.x (最新稳定版)
- **nlohmann-json**: 3.x (最新稳定版)
- **GTest**: 1.14+ (最新稳定版)

---

## 更新依赖

### 使用 vcpkg

```powershell
# 更新 vcpkg 本身
cd [vcpkg root]
git pull
.\bootstrap-vcpkg.bat

# 升级所有包
vcpkg upgrade --no-dry-run
```

---

## 下一步

完成依赖安装后，可以：

1. 构建项目：`cmake --build build --config Debug`
2. 运行测试：`cd build && ctest -C Debug --output-on-failure`
3. 运行主程序：`./build/pfh_server.exe`

---

## 参考文档

- [vcpkg 官方文档](https://vcpkg.io/)
- [spdlog GitHub](https://github.com/gabime/spdlog)
- [nlohmann-json GitHub](https://github.com/nlohmann/json)
- [GoogleTest GitHub](https://github.com/google/googletest)
