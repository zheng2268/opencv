#ifndef _DMA_BUF_POOL_
#define _DMA_BUF_POOL_
#include "aw_dmabuf.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include "mutex"

class AWDmaBufPool {
public:
    /**
     * @brief 构造函数
     * @param buffer_size 每个缓冲区的大小（字节）
     * @param max_buffers 最大缓冲区数量
     */
    AWDmaBufPool(size_t buffer_size, size_t max_buffers = 100);

    ~AWDmaBufPool();

    // 禁止拷贝
    AWDmaBufPool(const AWDmaBufPool&) = delete;
    AWDmaBufPool& operator=(const AWDmaBufPool&) = delete;

    /**
     * @brief 申请一个DMA缓冲区
     * @return DmaBuf智能指针
     */
    std::shared_ptr<AWDmaBuf> allocate();

    /**
     * @brief 释放DMA缓冲区（由DmaBuf的删除器调用）
     * @param id 要释放的缓冲区ID
     */
    void deallocate(DmaBufId id);

    /**
     * @brief 预分配指定数量的缓冲区
     * @param count 预分配的缓冲区数量
     */
    void preallocate(size_t count);

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

    Stats get_stats();

    /**
     * @brief 获取缓冲区大小
     * @return 缓冲区大小（字节）
     */
    size_t buffer_size() const;

    /**
     * @brief 获取最大缓冲区数量
     * @return 最大数量
     */
    size_t max_buffers() const;

    /**
     * @brief 获取当前分配的缓冲区数量
     * @return 已分配数量
     */
    size_t allocated_count();

private:
    struct BufferInfo {
        int fd;                    // 文件描述符
        size_t size;               // 缓冲区大小
        DmaBufId id;               // 缓冲区ID
        bool in_use;               // 是否正在使用
    };

    std::vector<BufferInfo> buffers_;      // 所有缓冲区信息
    std::vector<DmaBufId> free_list_;      // 空闲缓冲区ID列表
    std::unordered_map<DmaBufId, std::weak_ptr<AWDmaBuf>> active_buffers_; // 活跃缓冲区

    size_t buffer_size_;                   // 每个缓冲区的大小
    size_t max_buffers_;                   // 最大缓冲区数量
    size_t next_id_;                       // 下一个可用的缓冲区ID

    std::mutex mutex_;                     // 互斥锁，保证线程安全

    int32_t dmaHeapFd_ = -1;

    /**
     * @brief 创建新的DMA缓冲区
     * @param size 缓冲区大小
     * @return 缓冲区信息，失败时fd为-1
     */
    BufferInfo create_buffer(size_t size);
    /**
     * @brief 从ID获取缓冲区信息
     * @param id 缓冲区ID
     * @return 缓冲区信息的指针，不存在返回nullptr
     */
    BufferInfo* get_buffer_info(DmaBufId id);

};

#endif //_DMA_BUF_POOL_