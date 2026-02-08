# Malloc Performance Debugging Guide

## 调试工具和方法 / Debugging Tools and Methods

### 1. 检查当前malloc配置 / Check Current Malloc Configuration

```bash
# 查看当前tunables设置
echo $GLIBC_TUNABLES

# 查看glibc版本
ldd --version

# 检查是否使用了特殊的malloc实现
ldd /path/to/your/app | grep -E 'malloc|libc'
```

### 2. 使用malloc_stats监控内存使用 / Monitor Memory Usage

```c
#include <malloc.h>

/* 在程序中调用 */
malloc_stats();

/* 或者使用mallinfo2 (glibc 2.33+) */
struct mallinfo2 info = mallinfo2();
printf("Total allocated: %zu\n", info.uordblks);
printf("Total free: %zu\n", info.fordblks);
```

### 3. 使用perf分析锁竞争 / Analyze Lock Contention with perf

```bash
# 记录futex系统调用（用于锁）
perf record -e 'syscalls:sys_enter_futex' -g -p <PID> -- sleep 30

# 或者记录所有事件
perf record -g -p <PID> -- sleep 30

# 查看报告
perf report

# 查看特定函数的热点
perf report --stdio | grep -A 10 "malloc\|free"

# 使用火焰图可视化
perf script | stackcollapse-perf.pl | flamegraph.pl > malloc_flamegraph.svg
```

### 4. 使用SystemTap跟踪malloc调用 / Trace malloc with SystemTap

```bash
# 跟踪malloc调用
stap -e '
  probe process("/lib64/libc.so.6").function("__libc_malloc") {
    printf("%s[%d]: malloc(%d)\n", execname(), pid(), $bytes)
  }
  probe process("/lib64/libc.so.6").function("__libc_free") {
    printf("%s[%d]: free(%p)\n", execname(), pid(), $mem)
  }
' -c './your_app'

# 统计malloc大小分布
stap -e '
  global sizes
  
  probe process("/lib64/libc.so.6").function("__libc_malloc") {
    sizes[$bytes] <<< 1
  }
  
  probe end {
    print(@hist_log(sizes))
  }
' -c './your_app'
```

### 5. 使用mtrace跟踪内存泄漏 / Trace Memory Leaks with mtrace

```bash
# 在代码中添加
#include <mcheck.h>

int main() {
    mtrace();  // 开始跟踪
    
    // your code
    
    muntrace(); // 停止跟踪
}

# 编译并运行
gcc -o app app.c
MALLOC_TRACE=/tmp/mtrace.log ./app

# 分析结果
mtrace app /tmp/mtrace.log
```

### 6. 使用valgrind的massif分析堆使用 / Heap Profiling with Massif

```bash
# 运行massif
valgrind --tool=massif --massif-out-file=massif.out ./your_app

# 查看结果
ms_print massif.out

# 查看堆快照
ms_print massif.out | less
```

### 7. 使用gdb调试arena状态 / Debug Arena State with gdb

```bash
gdb ./your_app

# 在malloc断点
(gdb) break __libc_malloc
(gdb) run

# 查看arena信息
(gdb) p main_arena
(gdb) p main_arena.mutex
(gdb) p main_arena.attached_threads

# 查看tcache
(gdb) p tcache
```

---

## 性能问题诊断流程 / Performance Issue Diagnosis Workflow

### Step 1: 确认问题 / Confirm the Issue

1. **对比基准测试**: 在glibc 2.28和2.34上运行相同的工作负载
2. **量化影响**: 测量吞吐量、延迟、CPU使用率的差异
3. **隔离问题**: 确认问题确实由malloc/free引起

```bash
# 使用time测量
time ./your_app

# 使用perf stat获取详细统计
perf stat -d ./your_app
```

### Step 2: 识别瓶颈 / Identify Bottleneck

```bash
# 查找最耗时的函数
perf record -g ./your_app
perf report --stdio | head -50

# 查找锁竞争
perf record -e cycles -e 'syscalls:sys_enter_futex' -g ./your_app
perf report

# 检查缓存未命中
perf stat -e cache-references,cache-misses ./your_app
```

### Step 3: 分析原因 / Analyze Root Cause

#### 检查arena竞争 / Check Arena Contention

```c
// 在程序中添加
#include <malloc.h>

void print_malloc_info() {
    malloc_stats();
    
    // 或使用malloc_info (输出XML)
    malloc_info(0, stdout);
}
```

#### 检查tcache效率 / Check Tcache Efficiency

```bash
# 使用特定的tunable测试
GLIBC_TUNABLES=glibc.malloc.tcache_count=0 ./your_app  # 禁用tcache
GLIBC_TUNABLES=glibc.malloc.tcache_count=20 ./your_app # 增大tcache
```

### Step 4: 测试解决方案 / Test Solutions

#### 方案1: 调整tunables

```bash
# 测试不同arena数量
for arena in 8 16 32 64; do
    echo "Testing arena_max=$arena"
    GLIBC_TUNABLES=glibc.malloc.arena_max=$arena \
        perf stat ./your_app 2>&1 | grep "seconds time elapsed"
done
```

#### 方案2: 使用替代分配器

```bash
# jemalloc
LD_PRELOAD=/usr/lib64/libjemalloc.so ./your_app

# tcmalloc
LD_PRELOAD=/usr/lib64/libtcmalloc.so ./your_app
```

#### 方案3: 应用级优化

- 实现内存池
- 减少分配频率
- 批量分配
- 对象复用

---

## 常见问题和解决方案 / Common Issues and Solutions

### 问题1: 高锁竞争 / High Lock Contention

**症状**:
- perf显示大量futex系统调用
- CPU使用率不高但性能差
- 增加线程数性能不提升

**解决方案**:
```bash
# 增加arena数量
export GLIBC_TUNABLES=glibc.malloc.arena_max=32

# 增大tcache
export GLIBC_TUNABLES=glibc.malloc.tcache_count=10:glibc.malloc.tcache_max=524288
```

### 问题2: 内存碎片 / Memory Fragmentation

**症状**:
- RSS持续增长
- malloc_stats显示大量free空间但无法复用

**解决方案**:
```bash
# 调整trim阈值
export GLIBC_TUNABLES=glibc.malloc.trim_threshold=131072

# 或在代码中
malloc_trim(0);  // 释放未使用的内存到系统
```

### 问题3: 大块分配慢 / Slow Large Allocations

**症状**:
- 大于mmap阈值的分配很慢
- perf显示mmap系统调用频繁

**解决方案**:
```bash
# 增大mmap阈值
export GLIBC_TUNABLES=glibc.malloc.mmap_threshold=2097152  # 2MB
```

### 问题4: tcache相关崩溃 / Tcache-related Crashes

**症状**:
- 随机崩溃在free()
- 双重释放错误

**解决方案**:
```bash
# 启用malloc调试 (glibc 2.34+)
LD_PRELOAD=libc_malloc_debug.so MALLOC_CHECK_=3 ./your_app

# 或禁用tcache进行测试
GLIBC_TUNABLES=glibc.malloc.tcache_count=0 ./your_app
```

---

## 监控脚本示例 / Monitoring Script Example

```bash
#!/bin/bash
# monitor_malloc.sh - 监控应用的malloc性能

PID=$1

if [ -z "$PID" ]; then
    echo "Usage: $0 <PID>"
    exit 1
fi

echo "Monitoring process $PID for malloc performance..."
echo "Press Ctrl+C to stop"
echo ""

# 持续监控
while true; do
    echo "=== $(date) ==="
    
    # 内存使用
    cat /proc/$PID/status | grep -E 'VmRSS|VmSize'
    
    # 锁等待
    perf stat -p $PID -e 'syscalls:sys_enter_futex' -I 1000 2>&1 | grep -v '^#' | head -1
    
    # malloc系统调用
    strace -c -p $PID -e trace=mmap,munmap -f -q 2>&1 | head -1
    
    echo ""
    sleep 5
done
```

---

## 参考资料 / References

### 官方文档
- [glibc malloc tunables](https://www.gnu.org/software/libc/manual/html_node/Memory-Allocation-Tunables.html)
- [malloc implementation](https://sourceware.org/glibc/wiki/MallocInternals)

### 工具文档
- [perf tutorial](https://perf.wiki.kernel.org/index.php/Tutorial)
- [SystemTap malloc examples](https://sourceware.org/systemtap/examples/)
- [valgrind massif manual](https://valgrind.org/docs/manual/ms-manual.html)

### 性能优化
- [Memory allocation strategies](https://mechanical-sympathy.blogspot.com/)
- [Lock-free programming](https://preshing.com/20120612/an-introduction-to-lock-free-programming/)
