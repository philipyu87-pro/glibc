# glibc Malloc Tunables Quick Reference

## 最常用的Tunables / Most Common Tunables

### Arena相关 / Arena-related

| Tunable | 默认值 | 推荐值 (Kunpeng 920) | 说明 |
|---------|--------|---------------------|------|
| `glibc.malloc.arena_max` | 0 (unlimited) | CPU数 × 2 | 最大arena数量，限制锁竞争 |
| `glibc.malloc.arena_test` | 2 (32-bit), 8 (64-bit) | CPU数 | 创建新arena前的竞争阈值 |

### Thread Cache相关 / Thread Cache-related

| Tunable | 默认值 | 推荐值 (Kunpeng 920) | 说明 |
|---------|--------|---------------------|------|
| `glibc.malloc.tcache_bins` | 64 | 64 | tcache bin数量 |
| `glibc.malloc.tcache_max` | 516 bytes | 1024-4096 | tcache最大缓存块大小 |
| `glibc.malloc.tcache_count` | 7 | 10-16 | 每个bin最大缓存数量 |

### 内存管理相关 / Memory Management-related

| Tunable | 默认值 | 说明 |
|---------|--------|------|
| `glibc.malloc.trim_threshold` | 128KB | 空闲内存返回系统的阈值 |
| `glibc.malloc.top_pad` | 0 | 扩展堆时的额外空间 |
| `glibc.malloc.mmap_threshold` | 128KB | 使用mmap的分配大小阈值 |
| `glibc.malloc.mmap_max` | 65536 | 最大mmap区域数 |

### 其他Tunables / Other Tunables

| Tunable | 默认值 | 说明 |
|---------|--------|------|
| `glibc.malloc.perturb` | 0 | 调试用：用指定值填充分配的内存 |
| `glibc.malloc.check` | 0 | 堆一致性检查级别 (需要libc_malloc_debug.so) |
| `glibc.pthread.stack_cache_size` | 40MB | 线程栈缓存大小 |

---

## 预设配置 / Preset Configurations

### 1. 默认配置 / Default Configuration
```bash
# 不设置tunables，使用glibc默认值
unset GLIBC_TUNABLES
```

### 2. 低并发优化 / Low Concurrency (1-8 threads)
```bash
export GLIBC_TUNABLES="\
glibc.malloc.arena_max=8:\
glibc.malloc.tcache_count=7"
```

### 3. 中并发优化 / Medium Concurrency (8-32 threads)
```bash
export GLIBC_TUNABLES="\
glibc.malloc.arena_max=16:\
glibc.malloc.tcache_count=10:\
glibc.malloc.tcache_max=1024"
```

### 4. 高并发优化 / High Concurrency (32-64 threads)
```bash
export GLIBC_TUNABLES="\
glibc.malloc.arena_max=32:\
glibc.malloc.tcache_count=12:\
glibc.malloc.tcache_max=2048"
```

### 5. 极高并发优化 / Very High Concurrency (64+ threads)
```bash
export GLIBC_TUNABLES="\
glibc.malloc.arena_max=64:\
glibc.malloc.tcache_count=16:\
glibc.malloc.tcache_max=4096:\
glibc.malloc.arena_test=32"
```

### 6. 内存敏感场景 / Memory-sensitive
```bash
export GLIBC_TUNABLES="\
glibc.malloc.arena_max=8:\
glibc.malloc.trim_threshold=65536:\
glibc.malloc.tcache_count=5:\
glibc.malloc.mmap_threshold=131072"
```

### 7. 调试配置 / Debug Configuration
```bash
# 需要预加载 libc_malloc_debug.so
LD_PRELOAD=/lib64/libc_malloc_debug.so \
GLIBC_TUNABLES="\
glibc.malloc.check=3:\
glibc.malloc.perturb=42"
```

---

## 设置方法 / How to Set Tunables

### 方法1: 环境变量 / Environment Variable
```bash
export GLIBC_TUNABLES="glibc.malloc.arena_max=16"
./your_app
```

### 方法2: 程序启动时设置 / Set at Program Launch
```bash
GLIBC_TUNABLES="glibc.malloc.arena_max=16" ./your_app
```

### 方法3: systemd服务 / systemd Service
```ini
[Service]
Environment="GLIBC_TUNABLES=glibc.malloc.arena_max=16"
ExecStart=/path/to/your_app
```

### 方法4: Shell配置文件 / Shell Configuration
```bash
# 在 ~/.bashrc 或 /etc/profile 中添加
export GLIBC_TUNABLES="glibc.malloc.arena_max=16"
```

---

## 查看当前设置 / View Current Settings

### 查看tunables环境变量 / View Tunables Environment
```bash
echo $GLIBC_TUNABLES
```

### 查看运行中程序的tunables / View Running Process Tunables
```bash
# 方法1: 通过环境变量
cat /proc/<PID>/environ | tr '\0' '\n' | grep GLIBC_TUNABLES

# 方法2: 通过LD_SHOW_AUXV (仅对新启动的进程)
LD_SHOW_AUXV=1 ./your_app
```

### 列出所有可用tunables / List All Available Tunables
```bash
ld.so --list-tunables
```

---

## 性能测试命令 / Performance Testing Commands

### 快速基准测试 / Quick Benchmark
```bash
# 使用默认设置
time ./your_app

# 使用优化设置
GLIBC_TUNABLES="glibc.malloc.arena_max=32:glibc.malloc.tcache_count=10" \
time ./your_app
```

### 详细性能分析 / Detailed Performance Analysis
```bash
# CPU性能
perf stat -d ./your_app

# 锁竞争分析
perf record -e 'syscalls:sys_enter_futex' -g ./your_app
perf report
```

### 内存使用监控 / Memory Usage Monitoring
```bash
# 使用/usr/bin/time获取详细信息
/usr/bin/time -v ./your_app

# 使用valgrind massif
valgrind --tool=massif ./your_app
ms_print massif.out.*
```

---

## 故障排查清单 / Troubleshooting Checklist

### 性能问题 / Performance Issues

- [ ] 确认tunables已正确设置 (`echo $GLIBC_TUNABLES`)
- [ ] 检查glibc版本 (`ldd --version`)
- [ ] 使用perf分析热点函数
- [ ] 检查是否有锁竞争（futex系统调用）
- [ ] 测试不同的arena_max值
- [ ] 测试不同的tcache_count值
- [ ] 考虑使用jemalloc或tcmalloc替代

### 内存问题 / Memory Issues

- [ ] 检查是否有内存泄漏 (valgrind)
- [ ] 查看malloc_stats输出
- [ ] 检查RSS增长趋势
- [ ] 测试降低arena_max
- [ ] 测试降低tcache_count
- [ ] 定期调用malloc_trim()

### 崩溃问题 / Crash Issues

- [ ] 启用malloc调试 (MALLOC_CHECK_=3)
- [ ] 使用libc_malloc_debug.so
- [ ] 使用valgrind检查内存错误
- [ ] 禁用tcache测试 (tcache_count=0)
- [ ] 检查是否有双重释放
- [ ] 检查是否有缓冲区溢出

---

## Kunpeng 920特定建议 / Kunpeng 920 Specific Recommendations

### 硬件特性 / Hardware Characteristics
- 架构: ARMv8.2-A
- 核心数: 最多64核
- L1 Cache: 64KB I + 64KB D per core
- L2 Cache: 512KB per core
- L3 Cache: 32-64MB shared

### 推荐配置 / Recommended Configuration

#### 通用服务器应用 / General Server Applications
```bash
export GLIBC_TUNABLES="\
glibc.malloc.arena_max=32:\
glibc.malloc.tcache_count=10:\
glibc.malloc.tcache_max=2048"
```

#### 数据库服务 / Database Services
```bash
export GLIBC_TUNABLES="\
glibc.malloc.arena_max=16:\
glibc.malloc.tcache_count=8:\
glibc.malloc.trim_threshold=262144:\
glibc.malloc.mmap_threshold=262144"
```

#### Web服务器 / Web Servers
```bash
export GLIBC_TUNABLES="\
glibc.malloc.arena_max=32:\
glibc.malloc.tcache_count=12:\
glibc.malloc.tcache_max=1024"
```

#### 科学计算 / Scientific Computing
```bash
export GLIBC_TUNABLES="\
glibc.malloc.arena_max=8:\
glibc.malloc.mmap_threshold=1048576:\
glibc.malloc.trim_threshold=524288"
```

---

## 额外资源 / Additional Resources

### 文档 / Documentation
- glibc manual: https://www.gnu.org/software/libc/manual/
- tunables guide: https://www.gnu.org/software/libc/manual/html_node/Tunables.html
- malloc internals: https://sourceware.org/glibc/wiki/MallocInternals

### 工具 / Tools
- perf: Linux性能分析工具
- valgrind: 内存调试和分析
- SystemTap: 动态跟踪
- gdb: 调试器

### 替代方案 / Alternatives
- jemalloc: http://jemalloc.net/
- tcmalloc: https://github.com/google/tcmalloc
- mimalloc: https://github.com/microsoft/mimalloc
