/*
 * Emscripten 阻塞 stdin — 用 emscripten_futex_wait 实现
 * 替换 std::cin 的 streambuf，使 getline() 在无数据时真正挂起 Worker 线程
 */

#pragma once

#ifdef __EMSCRIPTEN__

#include <emscripten/threading.h>
#include <atomic>
#include <streambuf>
#include <cstring>

class EmscriptenStdinBuf : public std::streambuf {
    static constexpr size_t BUF_SIZE = 4096;
    char        buf[BUF_SIZE];
    std::atomic<size_t> read_idx{0};   // Worker 读取位置
    std::atomic<size_t> write_idx{0};  // JS 写入位置
    std::atomic<int>    signal{0};     // futex 信号量: 0=无数据, 1=有数据

public:
    EmscriptenStdinBuf() {
        // get area 初始为空，触发 underflow()
        setg(buf, buf, buf);
    }

    // ★ JS 侧通过 Module._push_stdin(data, len) 调用此函数
    void push_data(const char* data, size_t len) {
        for (size_t i = 0; i < len; i++) {
            size_t wi = write_idx.load(std::memory_order_relaxed);
            buf[wi % BUF_SIZE] = data[i];
            write_idx.store(wi + 1, std::memory_order_release);
        }
        signal.store(1, std::memory_order_release);
        emscripten_futex_wake(&signal, 1);
    }

protected:
    // std::streambuf::underflow — get area 空时被调用
    int underflow() override {
        // 等待 JS 推送数据
        while (read_idx.load(std::memory_order_acquire) >=
               write_idx.load(std::memory_order_acquire))
        {
            signal.store(0, std::memory_order_release);
            emscripten_futex_wait(&signal, 0, INFINITY);
        }

        size_t ri = read_idx.load(std::memory_order_relaxed);
        char c = buf[ri % BUF_SIZE];
        read_idx.store(ri + 1, std::memory_order_relaxed);

        // 把字符放入 get area
        buf[0] = c;
        setg(buf, buf, buf + 1);
        return traits_type::to_int_type(c);
    }
};

#endif
