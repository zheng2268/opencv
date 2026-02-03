#include "aw_dmabuf_pool.h"
#include "aw_dmabuf.h"
#include <memory>
#include <bits/stdint-intn.h>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <unistd.h>
#include "mutex"
#include "linux/dma-buf.h"
#include "linux/dma-heap.h"
#include "cerrno"
#include <iostream>
#include <sys/ioctl.h>
#include "opencv2/core/utils/logger.hpp"

#define DEV_NAME        "/dev/dma_heap/system"

/**
    * @brief 构造函数
    * @param buffer_size 每个缓冲区的大小（字节）
    * @param max_buffers 最大缓冲区数量
    */
AWDmaBufPool::AWDmaBufPool(size_t buffer_size, size_t max_buffers)
    : buffer_size_(buffer_size), max_buffers_(max_buffers), next_id_(0) {
    if (buffer_size == 0) {
        throw std::invalid_argument("Buffer size must be greater than 0");
    }

    dmaHeapFd_ = open(DEV_NAME, O_RDONLY | O_CLOEXEC);
    if( dmaHeapFd_ < 0 ){
        throw std::invalid_argument("open dma_heap failed");
    }
}

AWDmaBufPool::~AWDmaBufPool() {
    // 销毁内存池时，确保所有缓冲区都被安全回收
    std::lock_guard<std::mutex> lock(mutex_);

    // 等待所有活跃缓冲区被释放
    for (auto& kv : active_buffers_) {
        if (auto buf = kv.second.lock()) {
            // 缓冲区仍然在使用中，这里可以选择等待或记录警告
            CV_LOG_ERROR(NULL, "Warning: AWDmaBuf with ID " << kv.first
                        << " is still active during pool destruction");
        }
    }
    for(auto buf : buffers_ ){
        close(buf.fd);
    }
    // 清理所有缓冲区
    buffers_.clear();
    free_list_.clear();
    active_buffers_.clear();

    if( dmaHeapFd_ > 0 ){
        close(dmaHeapFd_);
        dmaHeapFd_ = -1;
    }
}

/**
    * @brief 申请一个DMA缓冲区
    * @return DmaBuf智能指针
    */
std::shared_ptr<AWDmaBuf> AWDmaBufPool::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);

    DmaBufId id = 0;
    BufferInfo* info = nullptr;
    // 首先尝试从空闲列表获取
    if (!free_list_.empty()) {
        id = free_list_.back();
        free_list_.pop_back();
        info = get_buffer_info(id);
        if (info) {
            info->in_use = true;
        }
    } else if (buffers_.size() < max_buffers_) {
        // 创建新的缓冲区
        auto new_info = create_buffer(buffer_size_);
        if (new_info.fd < 0) {
            throw std::runtime_error("Failed to create DMA buffer");
        }

        new_info.in_use = true;
        id = new_info.id;
        buffers_.push_back(new_info);
        info = &buffers_.back();
    } else {
        throw std::runtime_error("DMA buffer pool exhausted");
    }

    if (!info) {
        throw std::runtime_error("Failed to allocate DMA buffer");
    }

    // 创建DmaBuf对象
    auto deleter = [this](AWDmaBuf* buf) {
        // 自定义删除器，将缓冲区归还给内存池
        deallocate(buf->id());
        delete buf;
    };

    auto dma_buf = std::shared_ptr<AWDmaBuf>(
        new AWDmaBuf(info->fd, info->size, id),
        deleter
    );

    // 记录活跃缓冲区
    active_buffers_[id] = dma_buf;

    return dma_buf;
}

/**
    * @brief 释放DMA缓冲区（由DmaBuf的删除器调用）
    * @param id 要释放的缓冲区ID
    */
void AWDmaBufPool::deallocate(DmaBufId id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto info = get_buffer_info(id);
    if (info) {
        info->in_use = false;
        free_list_.push_back(id);
    }

    // 从活跃缓冲区中移除
    active_buffers_.erase(id);
}

/**
    * @brief 预分配指定数量的缓冲区
    * @param count 预分配的缓冲区数量
    */
void AWDmaBufPool::preallocate(size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (count > max_buffers_) {
        count = max_buffers_;
    }

    for (size_t i = 0; i < count; ++i) {
        if (buffers_.size() >= max_buffers_) {
            break;
        }

        auto info = create_buffer(buffer_size_);
        if (info.fd < 0) {
            throw std::runtime_error("Failed to create DMA buffer during preallocation");
        }

        info.in_use = false;
        buffers_.push_back(info);
        free_list_.push_back(info.id);
    }
}

/**
    * @brief 获取内存池统计信息
    * @return 包含统计信息的结构体
    */
struct Stats {
    size_t total_buffers;      // 总缓冲区数量
    size_t free_buffers;       // 空闲缓冲区数量
    size_t active_buffers;     // 活跃缓冲区数量
    size_t buffer_size;        // 缓冲区大小
};

AWDmaBufPool::Stats AWDmaBufPool::get_stats() {
    std::lock_guard<std::mutex> lock(mutex_);

    Stats stats;
    stats.total_buffers = buffers_.size();
    stats.free_buffers = free_list_.size();
    stats.active_buffers = active_buffers_.size();
    stats.buffer_size = buffer_size_;

    return stats;
}

/**
    * @brief 获取缓冲区大小
    * @return 缓冲区大小（字节）
    */
size_t AWDmaBufPool::buffer_size() const { return buffer_size_; }

/**
    * @brief 获取最大缓冲区数量
    * @return 最大数量
    */
size_t AWDmaBufPool::max_buffers() const { return max_buffers_; }

/**
    * @brief 获取当前分配的缓冲区数量
    * @return 已分配数量
    */
size_t AWDmaBufPool::allocated_count() {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffers_.size();
}

/**
    * @brief 创建新的DMA缓冲区
    * @param size 缓冲区大小
    * @return 缓冲区信息，失败时fd为-1
    */
AWDmaBufPool::BufferInfo AWDmaBufPool::create_buffer(size_t size) {
    BufferInfo info;
    info.fd = -1;
    info.size = size;
    info.id = next_id_++;
    info.in_use = false;

    struct dma_heap_allocation_data heap_data{
        .len = size,  // length of data to be allocated in bytes
        .fd_flags = O_RDWR | O_CLOEXEC,  // permissions for the memory to be allocated
    };
    ioctl(dmaHeapFd_, DMA_HEAP_IOCTL_ALLOC, &heap_data);
    info.fd = heap_data.fd;
    return info;
}

/**
    * @brief 从ID获取缓冲区信息
    * @param id 缓冲区ID
    * @return 缓冲区信息的指针，不存在返回nullptr
    */
AWDmaBufPool::BufferInfo* AWDmaBufPool::get_buffer_info(DmaBufId id) {
    for (auto& info : buffers_) {
        if (info.id == id) {
            return &info;
        }
    }
    return nullptr;
}