#include <cstdint>
class StringHeap {
public:
    StringHeap(void* memory, uint64_t size)
        : memory_(static_cast<uint8_t*>(memory)),
          size_(size) {}

    void* ptr(uint64_t offset) {
        if (offset >= size_)
            return nullptr;

        return memory_ + offset;
    }

    const void* ptr(uint64_t offset) const {
        if (offset >= size_)
            return nullptr;

        return memory_ + offset;
    }

    uint64_t allocate(uint64_t size);

    void deallocate(uint64_t offset);

    uint8_t* getmemory();

private:
    uint8_t* memory_;
    uint64_t size_;
};
