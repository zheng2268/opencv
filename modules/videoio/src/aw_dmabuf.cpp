#include "aw_dmabuf_pool.h"
#include <iostream>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstring>
#include <stdexcept>

AWDmaBuf::AWDmaBuf(int fd, size_t size, DmaBufId id)
    : fd_(fd), mapped_addr_(nullptr), size_(size), id_(id) {
    if (fd_ < 0) {
        throw std::runtime_error("Invalid DMA buffer file descriptor");
    }
}

AWDmaBuf::~AWDmaBuf() {
    // 自动解除映射
    if (mapped_addr_ != nullptr) {
        munmap(mapped_addr_, size_);
        mapped_addr_ = nullptr;
    }
    // 关闭文件描述符
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

/**
    * @brief 获取DMA缓冲区的文件描述符
    * @return 文件描述符
    */
int AWDmaBuf::fd() const { return fd_; }

/**
    * @brief 获取缓冲区大小
    * @return 大小（字节）
    */
size_t AWDmaBuf::size() const { return size_; }

/**
    * @brief 获取缓冲区ID
    * @return 唯一ID
    */
DmaBufId AWDmaBuf::id() const { return id_; }

/**
    * @brief 将DMA缓冲区映射到虚拟内存空间
    * @param prot 映射保护标志（如PROT_READ, PROT_WRITE）
    * @return 映射后的虚拟地址，失败返回nullptr
    */
void* AWDmaBuf::map(int prot) {
    if (mapped_addr_ != nullptr) {
        // 已经映射过，返回现有地址
        return mapped_addr_;
    }

    mapped_addr_ = mmap(nullptr, size_, prot, MAP_SHARED, fd_, 0);
    if (mapped_addr_ == MAP_FAILED) {
        mapped_addr_ = nullptr;
        throw std::runtime_error("Failed to mmap DMA buffer");
    }

    return mapped_addr_;
}

/**
    * @brief 解除映射（通常不需要手动调用，析构时会自动调用）
    */
void AWDmaBuf::unmap() {
    if (mapped_addr_ != nullptr) {
        munmap(mapped_addr_, size_);
        mapped_addr_ = nullptr;
    }
}

/**
    * @brief 获取映射的虚拟地址（需要先调用map）
    * @return 虚拟地址，未映射时返回nullptr
    */
void* AWDmaBuf::addr() const { return mapped_addr_; }

/**
    * @brief 同步缓冲区内容
    * @param start 起始偏移
    * @param length 长度
    * @param flags 同步标志
    * @return 成功返回true
    */
bool AWDmaBuf::sync(uint64_t start, uint64_t length, uint32_t flags) {
    struct dma_buf_sync sync_args = {0};
    sync_args.flags = flags;

    // 如果不指定长度，则同步整个缓冲区
    if (length == 0) {
        length = size_;
    }

    // 这里简化处理，实际使用时可能需要处理部分同步
    if (ioctl(fd_, DMA_BUF_IOCTL_SYNC, &sync_args) < 0) {
        return false;
    }

    return true;
}