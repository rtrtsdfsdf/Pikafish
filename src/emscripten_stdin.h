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
#include <iostream>

class EmscriptenStdinBuf : public std::streambuf {
    static constexpr size_t BUF_SIZE = 4096;
    char buf[BUF_SIZE];
    std::atomic<size_t> read_idx{0};
    std::atomic<size_t> write_idx{0};
    std::atomic<int> avail{0};   // 可读字符数
    char m_get_char;

public:
    EmscriptenStdinBuf() {
        // get area 初始为空，触发 underflow()
        setg(buf, buf, buf);
    }

    void push_data(const char* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            size_t wi = write_idx.load(std::memory_order_relaxed);
            size_t ri = read_idx.load(std::memory_order_acquire);
            if (wi - ri >= BUF_SIZE) {
                // 缓冲区满，丢弃或阻塞（这里选择丢弃当前字符）
                std::cout << "buffer overflow " << std::endl;
                // continue;
            }
            buf[wi % BUF_SIZE] = data[i];
            write_idx.store(wi + 1, std::memory_order_release);
            // 每写入一个字符就增加可读计数并唤醒
            avail.fetch_add(1, std::memory_order_release);
            emscripten_futex_wake(&avail, 1);
        }
    }

protected:
    int underflow() override {
        while (avail.load(std::memory_order_acquire) == 0) {
            // 没有数据，等待生产者增加 avail 并唤醒
            emscripten_futex_wait(&avail, 0, INFINITY);
        }
        avail.fetch_sub(1, std::memory_order_acquire);

        size_t ri = read_idx.load(std::memory_order_relaxed);
        m_get_char = buf[ri % BUF_SIZE];
        read_idx.store(ri + 1, std::memory_order_relaxed);
        setg(&m_get_char, &m_get_char, &m_get_char + 1);
        return traits_type::to_int_type(m_get_char);
    }
};

#endif
