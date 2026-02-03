#ifndef _DMA_BUF_
#define _DMA_BUF_
#include "memory"
#include <sys/mman.h>
#include <linux/dma-buf.h>

class DmaBufPool;

// DMA缓冲区的唯一标识符
using DmaBufId = uint64_t;

/**
 * @class AWDmaBuf
 * @brief 代表一个DMA缓冲区，使用智能指针管理资源
 */
class AWDmaBuf {
public:
    ~AWDmaBuf();

    /**
     * @brief 获取DMA缓冲区的文件描述符
     * @return 文件描述符
     */
    int fd() const;

    /**
     * @brief 获取缓冲区大小
     * @return 大小（字节）
     */
    size_t size() const;

    /**
     * @brief 获取缓冲区ID
     * @return 唯一ID
     */
    DmaBufId id() const;

    /**
     * @brief 将DMA缓冲区映射到虚拟内存空间
     * @param prot 映射保护标志（如PROT_READ, PROT_WRITE）
     * @return 映射后的虚拟地址，失败返回nullptr
     */
    void* map(int prot = PROT_READ | PROT_WRITE);

    /**
     * @brief 解除映射（通常不需要手动调用，析构时会自动调用）
     */
    void unmap();

    /**
     * @brief 获取映射的虚拟地址（需要先调用map）
     * @return 虚拟地址，未映射时返回nullptr
     */
    void* addr() const;

    /**
     * @brief 同步缓冲区内容
     * @param start 起始偏移
     * @param length 长度
     * @param flags 同步标志
     * @return 成功返回true
     */
    bool sync(uint64_t start = 0, uint64_t length = 0, uint32_t flags = DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE);

    // 友元类，允许DmaBufPool访问私有构造函数
    friend class AWDmaBufPool;

private:
    int fd_;                     // DMA缓冲区的文件描述符
    void* mapped_addr_;          // 映射的虚拟地址
    size_t size_;                // 缓冲区大小
    DmaBufId id_;                // 缓冲区ID

    AWDmaBuf(int fd, size_t size, DmaBufId id);

    AWDmaBuf(const AWDmaBuf&) = delete;
    AWDmaBuf& operator=(const AWDmaBuf&) = delete;

};

#endif //_DMA_BUF_