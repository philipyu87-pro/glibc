# glibc malloc/free Performance Analysis: 2.28 vs 2.34 on Kunpeng 920

## 问题描述 / Problem Statement

glibc 2.34版本对比glibc 2.28版本在Kunpeng 920硬件（ARM架构）上，当多线程大压力下malloc和free的锁竞争消耗变大。

Comparing glibc 2.34 with glibc 2.28 on Kunpeng 920 hardware (ARM architecture), there is increased lock contention overhead in malloc/free operations under heavy multi-threaded workloads.

---

## 主要变化概述 / Summary of Major Changes

### Version 2.34 (August 2021)

**关键malloc相关变化 / Key malloc-related changes:**

1. **移除malloc hooks / Malloc hooks removed** (Major Impact)
   - `__malloc_hook`, `__realloc_hook`, `__memalign_hook`, `__free_hook` 从API中移除
   - `__morecore` 和 `__after_morecore_hook` hooks被移除
   - 调试功能（MALLOC_CHECK_, mtrace, mcheck）默认禁用，需要预加载 `libc_malloc_debug.so`
   - **影响**: 这些变化可能减少了运行时开销，但也移除了某些调试能力

2. **库整合 / Library integration**
   - libpthread整合到libc中
   - **潜在影响**: 多线程应用的符号解析和初始化可能有变化

3. **新增tunable**
   - `glibc.pthread.stack_cache_size`: 配置线程栈缓存大小
   - **相关性**: 可能影响线程创建/销毁的性能

### Version 2.33 (February 2021)

**关键变化 / Key changes:**

1. **mallinfo2函数**
   - 添加了mallinfo2函数以支持更大的统计值
   - mallinfo函数被标记为deprecated

2. **Bug修复**
   - [27237] malloc: deadlock in malloc/tst-malloc-stats-cancellation
   - **影响**: 修复了malloc统计中的死锁问题

### Version 2.32 (August 2020)

**关键变化 / Key changes:**

1. **AArch64分支保护 / AArch64 Branch Protection**
   - 在Kunpeng 920（ARMv8.5-a）上支持标准分支保护
   - BTI（Branch Target Identification）和PAC-RET（Pointer Authentication）
   - **影响**: 安全特性可能带来轻微性能开销

2. **Bug修复**
   - [25733] malloc: mallopt(M_MXFAST) can set global_max_fast to 0
   - [25942] nptl: Deadlock on stack_cache_lock between __nptl_setxid and exiting detached thread
   - **影响**: 修复了fastbin和线程栈缓存的死锁问题

### Version 2.31 (February 2020)

无直接malloc/free相关的重大变化。

### Version 2.30 (February 2019)

**关键变化 / Key changes:**

1. **内存分配限制**
   - malloc/calloc/realloc等函数现在拒绝大于PTRDIFF_MAX的分配
   - **影响**: 防止指针减法溢出，但不影响正常使用场景

2. **Bug修复**
   - [24531] malloc: Malloc tunables give tcache assertion failures
   - [23733] malloc: Check the count before calling tcache_get()
   - **影响**: tcache相关的bug修复

### Version 2.29 (February 2019)

**关键变化 / Key changes:**

1. **powerpc64le TLE (Transactional Lock Elision)**
   - 仅在内核支持时启用TLE
   - **不适用于ARM**: 此变化不影响Kunpeng 920

2. **Bug修复**
   - [23907] malloc: Incorrect double-free malloc tcache check disregards tcache size
   - **影响**: tcache双重释放检测修复

---

## malloc实现分析 / malloc Implementation Analysis

### 核心架构组件 / Core Architecture Components

#### 1. Arena（竞技场）管理

**目的**: 通过多个arena减少锁竞争

```c
struct malloc_state {
    mutex_t mutex;                    // 每个arena的互斥锁
    int flags;                        // 状态标志
    mfastbinptr fastbinsY[NFASTBINS]; // fastbin数组
    mchunkptr top;                    // top chunk
    mchunkptr bins[NBINS * 2];        // small/large bins
    unsigned int attached_threads;     // 附加的线程数
    // ...
};
```

**关键点**:
- 每个arena有独立的mutex
- 多个arena允许并发操作
- 通过`glibc.malloc.arena_test`和`glibc.malloc.arena_max`控制

#### 2. Thread Cache (tcache)

**目的**: 提供线程本地缓存，避免锁竞争

```c
typedef struct tcache_perthread_struct {
    uint16_t counts[TCACHE_MAX_BINS];
    tcache_entry *entries[TCACHE_MAX_BINS];
} tcache_perthread_struct;
```

**关键特性**:
- 每个线程独立的tcache
- 小块分配/释放无需获取arena锁
- 默认每个bin最多缓存7个chunk
- 可通过tunables配置

**相关tunables**:
- `glibc.malloc.tcache_max`
- `glibc.malloc.tcache_bins`  
- `glibc.malloc.tcache_count`

#### 3. Fastbins

**目的**: 快速分配小块内存

- 单链表LIFO结构
- 不合并相邻chunk
- 需要获取arena锁

### 锁竞争分析 / Lock Contention Analysis

#### 主要锁机制 / Main Locking Mechanisms

1. **Per-arena mutex** (`arena->mutex`)
   - 保护arena内部数据结构
   - 大部分malloc/free操作需要获取

2. **free_list_lock**
   - 保护空闲arena链表
   - 线程首次分配时获取

3. **list_lock**
   - 保护arena链表的并发修改

#### 锁竞争场景 / Lock Contention Scenarios

**高竞争场景**:
1. 多个线程同时从同一arena分配
2. tcache已满，需要访问arena
3. 大块内存分配（跳过tcache）
4. malloc_stats等统计操作

**优化路径**:
- tcache命中: 无锁操作
- fastbin命中: 需要arena锁（短时间）
- small/large bin: 需要arena锁（较长时间）

---

## 2.28 到 2.34 的性能影响分析 / Performance Impact Analysis

### 可能导致性能下降的因素 / Factors Potentially Causing Performance Degradation

#### 1. 安全特性开销 / Security Feature Overhead

**AArch64分支保护 (2.32引入)**:
- BTI和PAC-RET在ARMv8.5-a上启用
- 每个函数调用/返回增加额外指令
- **预估影响**: 1-3%性能开销

#### 2. Hook移除的副作用 / Side Effects of Hook Removal (2.34)

虽然hook移除应该减少开销，但可能带来:
- 代码路径变化
- 内联决策变化
- 缓存行为变化

#### 3. 库整合影响 / Library Integration Impact (2.34)

libpthread整合到libc可能导致:
- 符号查找路径变化
- TLS初始化时机变化
- 潜在的初始化顺序问题

### 可能改善性能的因素 / Factors Potentially Improving Performance

#### 1. Bug修复 / Bug Fixes

- 死锁修复减少锁等待时间
- tcache改进提高命中率

#### 2. Hook移除 / Hook Removal

- 减少每次分配/释放的检查
- 更简洁的代码路径

---

## 针对Kunpeng 920的优化建议 / Optimization Recommendations for Kunpeng 920

### 1. Tunables配置 / Tunables Configuration

```bash
# 增加arena数量以减少锁竞争
export GLIBC_TUNABLES=glibc.malloc.arena_max=16

# 增加tcache大小以提高命中率
export GLIBC_TUNABLES=glibc.malloc.tcache_count=10:glibc.malloc.tcache_max=524288

# 综合配置
export GLIBC_TUNABLES=glibc.malloc.arena_max=16:glibc.malloc.tcache_count=10:glibc.malloc.tcache_max=524288:glibc.pthread.stack_cache_size=41943040
```

**推荐值** (基于高并发场景):
- `arena_max`: CPU核心数的1-2倍
- `tcache_count`: 7-10 (默认7)
- `tcache_max`: 默认值或更大，取决于工作负载

### 2. 编译选项 / Compilation Options

对于性能敏感的应用，考虑:

```bash
# 禁用分支保护（如果安全要求允许）
# 在configure时使用: --disable-default-branch-protection

# 使用LTO和PGO
CFLAGS="-O3 -flto -fuse-linker-plugin"
```

### 3. 应用级优化 / Application-level Optimizations

1. **使用内存池**: 减少malloc/free调用频率
2. **批量分配**: 一次分配多个对象
3. **对象复用**: 避免频繁分配/释放
4. **大小对齐**: 利用tcache的bin大小

### 4. 监控和诊断 / Monitoring and Diagnostics

```bash
# 检查malloc统计
LD_PRELOAD=libc_malloc_debug.so MALLOC_TRACE=/tmp/mtrace.log ./your_app

# 使用perf分析锁竞争
perf record -e 'syscalls:sys_enter_futex' -g ./your_app
perf report

# 检查arena使用情况
gdb ./your_app
(gdb) call malloc_stats()
```

---

## 关键差异总结 / Summary of Key Differences

| 方面 | glibc 2.28 | glibc 2.34 | 影响 |
|------|-----------|-----------|------|
| malloc hooks | 可用 | 已移除 | 减少开销，但失去调试能力 |
| 调试功能 | 默认启用 | 需要预加载DSO | 正常运行时开销更低 |
| libpthread | 独立库 | 整合到libc | 可能影响初始化 |
| AArch64安全特性 | 无 | BTI/PAC-RET支持 | 轻微性能开销 |
| 死锁修复 | 已知问题 | 已修复 | 提高稳定性 |
| tcache | 基本功能 | 改进的错误检查 | 更可靠 |

---

## 性能测试建议 / Performance Testing Recommendations

### 基准测试 / Benchmarks

1. **malloc-test**: 微基准测试
   ```bash
   # 编译测试程序
   gcc -O2 -pthread benchmark.c -o benchmark
   
   # 在2.28和2.34上运行
   ./benchmark --threads=32 --iterations=1000000
   ```

2. **实际工作负载**: 使用真实应用场景
   - 监控CPU使用率
   - 监控锁等待时间（futex系统调用）
   - 监控内存分配延迟

3. **压力测试**: 
   - 逐渐增加线程数
   - 监控性能拐点
   - 确定最佳arena配置

### 性能分析工具 / Performance Analysis Tools

```bash
# 1. 使用perf分析
perf record -g --call-graph dwarf -F 99 ./your_app
perf report

# 2. 使用valgrind检查内存
valgrind --tool=massif ./your_app

# 3. 使用SystemTap监控malloc
stap -e 'probe process("/lib64/libc.so.6").function("__libc_malloc") { printf("%s\n", execname()) }'
```

---

## 结论和建议 / Conclusions and Recommendations

### 可能的性能下降原因 / Possible Reasons for Performance Degradation

1. **安全特性开销**: AArch64分支保护在Kunpeng 920上的开销
2. **库整合副作用**: libpthread整合可能改变初始化行为
3. **配置不当**: 默认的arena/tcache配置可能不适合高并发场景
4. **编译选项**: 不同的编译优化级别或标志

### 推荐行动 / Recommended Actions

1. **立即行动**:
   - 调整malloc tunables（arena_max, tcache参数）
   - 测试不同配置的性能影响

2. **短期行动**:
   - 使用性能分析工具定位瓶颈
   - 考虑应用级内存管理优化

3. **长期行动**:
   - 考虑使用替代的内存分配器（jemalloc, tcmalloc）
   - 优化应用的内存分配模式

### 需要进一步调查 / Further Investigation Needed

1. 获取实际的性能指标对比
2. 使用perf/ftrace分析锁竞争
3. 测试不同tunable配置的影响
4. 评估替代分配器的适用性

---

## 参考资料 / References

- glibc源代码: `malloc/malloc.c`, `malloc/arena.c`
- glibc NEWS文件: 版本2.28-2.34的变更日志
- malloc tunables文档: `manual/tunables.texi`
- Kunpeng 920技术文档: ARMv8.2-a架构

---

## 附录: 相关源文件 / Appendix: Relevant Source Files

### 核心实现文件 / Core Implementation Files

1. **malloc/malloc.c** (~10,000行)
   - 主分配器实现
   - tcache, fastbin, small/large bin逻辑
   - malloc(), free(), realloc()等函数

2. **malloc/arena.c** (~1,000行)
   - Arena管理
   - 多线程锁机制
   - Arena创建和销毁

3. **malloc/malloc-internal.h**
   - 内部接口定义
   - fork处理
   - 线程清理

### 架构相关文件 / Architecture-specific Files

对于ARM/AArch64:
- `sysdeps/aarch64/` - AArch64特定实现
- `sysdeps/unix/sysv/linux/aarch64/` - Linux AArch64系统调用

### 配置文件 / Configuration Files

- `elf/dl-tunables.list` - tunables定义
- `manual/tunables.texi` - tunables文档
