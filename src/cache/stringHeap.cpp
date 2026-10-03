#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

class StringHeap {
public:
    static constexpr uint64_t MAGIC = 0x5354524845415031ULL;
    static constexpr uint32_t VERSION = 1;

    static constexpr uint64_t ALIGNMENT = 8;

    /*
        Shared memory layout:

        +-----------------------+
        | HeapHeader            |
        +-----------------------+
        | BlockHeader           |
        +-----------------------+
        | payload               |
        +-----------------------+
        | BlockHeader           |
        +-----------------------+
        | payload               |
        +-----------------------+
    */

    struct alignas(8) HeapHeader {
        uint64_t magic;
        uint32_t version;
        uint32_t reserved;

        uint64_t total_size;
        uint64_t first_block;

        // 0 = unlocked
        // 1 = locked
        std::atomic<uint32_t> lock;
    };

    struct alignas(8) BlockHeader {
        uint64_t size;       // payload size
        uint64_t prev;      // offset of previous block header
        uint64_t next;      // offset of next block header

        uint32_t free;      // 1 = free, 0 = used
        uint32_t reserved;
    };

    struct Block {
        uint64_t offset;
        uint64_t size;
    };

public:
    StringHeap() = default;

    StringHeap(void* memory, uint64_t size) {
        attach(memory, size);
    }

    /*
        Initialize NEW shared memory.
        Must be called only once by owner.
    */
    static bool initialize(
        void* memory,
        uint64_t size
    ) {
        if (!memory)
            return false;

        if (size < minimum_size())
            return false;

        auto* header =
            static_cast<HeapHeader*>(memory);

        header->magic = MAGIC;
        header->version = VERSION;
        header->reserved = 0;
        header->total_size = size;

        header->first_block =
            align_up(sizeof(HeapHeader));

        header->lock.store(
            0,
            std::memory_order_relaxed
        );

        auto* block =
            block_at(
                memory,
                header->first_block
            );

        const uint64_t block_header_size =
            sizeof(BlockHeader);

        block->size =
            size
            - header->first_block
            - block_header_size;

        block->prev = 0;
        block->next = 0;
        block->free = 1;
        block->reserved = 0;

        return true;
    }

    /*
        Attach to existing shared memory.
    */
    bool attach(
        void* memory,
        uint64_t size
    ) {
        if (!memory)
            return false;

        auto* header =
            static_cast<HeapHeader*>(memory);

        if (header->magic != MAGIC)
            return false;

        if (header->version != VERSION)
            return false;

        if (header->total_size > size)
            return false;

        memory_ = static_cast<uint8_t*>(memory);
        size_ = size;
        header_ = header;

        return true;
    }

    bool valid() const {
        return
            memory_ != nullptr &&
            header_ != nullptr;
    }

    uint64_t total_size() const {
        return header_
            ? header_->total_size
            : 0;
    }

    /*
        Allocate payload.
        Returns OFFSET, not pointer.
        0 means failure.
    */
    uint64_t allocate(uint64_t size) {
        if (!valid() || size == 0)
            return 0;

        size = align_up(size);

        LockGuard guard(*this);

        uint64_t current =
            header_->first_block;

        while (current != 0) {
            auto* block =
                block_at(memory_, current);

            if (block->free &&
                block->size >= size) {

                return allocate_from_block(
                    current,
                    size
                );
            }

            current = block->next;
        }

        return 0;
    }

    /*
        Free payload by offset.
    */
    bool deallocate(uint64_t payload_offset) {
        if (!valid() || payload_offset == 0)
            return false;

        LockGuard guard(*this);

        uint64_t block_offset =
            payload_offset - sizeof(BlockHeader);

        if (!valid_block_offset(block_offset))
            return false;

        auto* block =
            block_at(memory_, block_offset);

        if (block->free)
            return false;

        block->free = 1;

        /*
            Merge with next first.
        */
        merge_next(block_offset);

        /*
            Then merge with previous.
        */
        if (block->prev != 0) {
            merge_next(block->prev);
        }

        return true;
    }

    /*
        Reallocate existing block.

        Returns new offset.
        Old offset may become invalid.

        If block can be extended in place,
        same offset is returned.
    */
    uint64_t reallocate(
        uint64_t old_offset,
        uint64_t new_size
    ) {
        if (!valid())
            return 0;

        if (old_offset == 0)
            return allocate(new_size);

        if (new_size == 0) {
            deallocate(old_offset);
            return 0;
        }

        new_size = align_up(new_size);

        LockGuard guard(*this);

        const uint64_t block_offset =
            old_offset - sizeof(BlockHeader);

        if (!valid_block_offset(block_offset))
            return 0;

        auto* block =
            block_at(memory_, block_offset);

        if (block->free)
            return 0;

        /*
            Already large enough.
        */
        if (block->size >= new_size) {

            split_block(
                block_offset,
                new_size
            );

            return old_offset;
        }

        /*
            Try to expand into next block.
        */
        if (block->next != 0) {
            auto* next =
                block_at(
                    memory_,
                    block->next
                );

            if (next->free) {
                const uint64_t combined =
                    block->size +
                    sizeof(BlockHeader) +
                    next->size;

                if (combined >= new_size) {

                    merge_next(block_offset);

                    split_block(
                        block_offset,
                        new_size
                    );

                    return old_offset;
                }
            }
        }

        /*
            Need completely new block.
        */
        uint64_t new_offset =
            allocate_locked(new_size);

        if (new_offset == 0)
            return 0;

        std::memcpy(
            ptr(new_offset),
            ptr(old_offset),
            block->size
        );

        /*
            Free old block.
        */
        block->free = 1;

        merge_next(block_offset);

        if (block->prev != 0)
            merge_next(block->prev);

        return new_offset;
    }

    void* ptr(uint64_t offset) {
        if (!valid())
            return nullptr;

        if (offset == 0)
            return nullptr;

        if (offset >= size_)
            return nullptr;

        return memory_ + offset;
    }

    const void* ptr(uint64_t offset) const {
        if (!valid())
            return nullptr;

        if (offset == 0)
            return nullptr;

        if (offset >= size_)
            return nullptr;

        return memory_ + offset;
    }

    /*
        Returns usable payload size.
    */
    uint64_t allocation_size(
        uint64_t payload_offset
    ) const {
        if (!valid() || payload_offset == 0)
            return 0;

        const uint64_t block_offset =
            payload_offset - sizeof(BlockHeader);

        if (!valid_block_offset(block_offset))
            return 0;

        const auto* block =
            block_at(
                memory_,
                block_offset
            );

        if (block->free)
            return 0;

        return block->size;
    }

private:

    static constexpr uint64_t minimum_size() {
        return
            sizeof(HeapHeader) +
            sizeof(BlockHeader) +
            ALIGNMENT;
    }

    static uint64_t align_up(uint64_t value) {
        return
            (value + ALIGNMENT - 1)
            & ~(ALIGNMENT - 1);
    }

    static BlockHeader* block_at(
        void* memory,
        uint64_t offset
    ) {
        return reinterpret_cast<BlockHeader*>(
            static_cast<uint8_t*>(memory)
            + offset
        );
    }

    static const BlockHeader* block_at(
        const void* memory,
        uint64_t offset
    ) {
        return reinterpret_cast<const BlockHeader*>(
            static_cast<const uint8_t*>(memory)
            + offset
        );
    }

    bool valid_block_offset(
        uint64_t offset
    ) const {
        if (offset < sizeof(HeapHeader))
            return false;

        if (offset + sizeof(BlockHeader) > size_)
            return false;

        return true;
    }

    uint64_t allocate_locked(
        uint64_t size
    ) {
        uint64_t current =
            header_->first_block;

        while (current != 0) {
            auto* block =
                block_at(memory_, current);

            if (block->free &&
                block->size >= size) {

                return allocate_from_block(
                    current,
                    size
                );
            }

            current = block->next;
        }

        return 0;
    }

    uint64_t allocate_from_block(
        uint64_t block_offset,
        uint64_t size
    ) {
        auto* block =
            block_at(
                memory_,
                block_offset
            );

        split_block(
            block_offset,
            size
        );

        block->free = 0;

        return
            block_offset +
            sizeof(BlockHeader);
    }

    /*
        Split:

        [ FREE BLOCK ------------------------- ]

        into:

        [ USED ][ FREE ]
    */
    void split_block(
        uint64_t block_offset,
        uint64_t requested_size
    ) {
        auto* block =
            block_at(
                memory_,
                block_offset
            );

        if (block->size <= requested_size)
            return;

        const uint64_t remaining =
            block->size
            - requested_size
            - sizeof(BlockHeader);

        if (remaining < ALIGNMENT)
            return;

        const uint64_t new_offset =
            block_offset
            + sizeof(BlockHeader)
            + requested_size;

        auto* new_block =
            block_at(
                memory_,
                new_offset
            );

        new_block->size = remaining;
        new_block->prev = block_offset;
        new_block->next = block->next;
        new_block->free = 1;
        new_block->reserved = 0;

        if (block->next != 0) {
            auto* next =
                block_at(
                    memory_,
                    block->next
                );

            next->prev = new_offset;
        }

        block->next = new_offset;
        block->size = requested_size;
    }

    /*
        Merge block with NEXT.
    */
    void merge_next(
        uint64_t block_offset
    ) {
        auto* block =
            block_at(
                memory_,
                block_offset
            );

        const uint64_t next_offset =
            block->next;

        if (next_offset == 0)
            return;

        auto* next =
            block_at(
                memory_,
                next_offset
            );

        if (!next->free)
            return;

        block->size +=
            sizeof(BlockHeader)
            + next->size;

        block->next =
            next->next;

        if (next->next != 0) {
            auto* after =
                block_at(
                    memory_,
                    next->next
                );

            after->prev =
                block_offset;
        }
    }

private:

    class LockGuard {
    public:
        explicit LockGuard(
            StringHeap& heap
        )
            : heap_(heap) {

            uint32_t expected = 0;

            while (
                !heap_
                    .header_
                    ->lock
                    .compare_exchange_weak(
                        expected,
                        1,
                        std::memory_order_acquire,
                        std::memory_order_relaxed
                    )
            ) {
                expected = 0;

#if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
#endif
            }
        }

        ~LockGuard() {
            heap_
                .header_
                ->lock
                .store(
                    0,
                    std::memory_order_release
                );
        }

        LockGuard(const LockGuard&) = delete;
        LockGuard& operator=(
            const LockGuard&
        ) = delete;

    private:
        StringHeap& heap_;
    };

private:
    uint8_t* memory_ = nullptr;
    uint64_t size_ = 0;
    HeapHeader* header_ = nullptr;
};
