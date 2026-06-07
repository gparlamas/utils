#pragma once

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>
#include <cmath>
#include <cassert>
#include <stdexcept>
#include <cstring>

/*
 * Arena style allocator that can use a pre-allocated buffer or allocate dynamically.
 * Can be initialized with an external buffer for zero-allocation scenarios.
 * Only allocates additional blocks when the provided buffer is exhausted.
 * It releases only dynamically allocated memory on destruction.
 * */
class MonotonicAllocator {
public:
    static constexpr size_t DefaultBlockSize = 4080;

private:
    struct storage {
        std::unique_ptr<std::byte[]> owned_buf;  // Only set if we own the memory
        std::byte* buf;                          // Points to either owned or external buffer
        size_t size;
        void* pos = nullptr;
        storage* next = nullptr;
        bool is_owned;
        
        // Constructor for owned (allocated) buffer
        explicit storage(size_t block_size) 
            : owned_buf(std::make_unique<std::byte[]>(block_size))
            , buf(owned_buf.get())
            , size(block_size)
            , pos(buf)
            , is_owned(true)
        {}
        
        // Constructor for external (non-owned) buffer
        storage(std::byte* external_buf, size_t buffer_size) noexcept
            : owned_buf(nullptr)
            , buf(external_buf)
            , size(buffer_size)
            , pos(buf)
            , is_owned(false)
        {}
        
        // Disable copy, allow move
        storage(const storage&) = delete;
        storage& operator=(const storage&) = delete;
        storage(storage&&) noexcept = default;
        storage& operator=(storage&&) noexcept = default;
    };

    storage* m_buffer = nullptr;
    storage* m_current = nullptr;
    size_t m_block_size;

public:
    ~MonotonicAllocator() noexcept {
        while (m_buffer) {
            auto buf = m_buffer;
            m_buffer = m_buffer->next;
            delete buf;
        }
    }

    // Constructor with external buffer (zero-allocation mode)
    explicit MonotonicAllocator(void* buffer, size_t buffer_size, size_t overflow_block_size = DefaultBlockSize) 
        : m_block_size(overflow_block_size)
    {
        assert(buffer != nullptr && "Buffer cannot be null");
        assert(buffer_size > 0 && "Buffer size must be positive");
        
        if (m_block_size == 0) {
            m_block_size = DefaultBlockSize;
        }
        
        m_buffer = new storage(static_cast<std::byte*>(buffer), buffer_size);
        m_current = m_buffer;
    }
    
    // Constructor that allocates its own buffer
    explicit MonotonicAllocator(size_t block_size = DefaultBlockSize) 
        : m_block_size(block_size)
    {
        if (m_block_size == 0) {
            m_block_size = DefaultBlockSize;
        }
        
        m_buffer = new storage(m_block_size);
        m_current = m_buffer;
    }

    // Explicitly delete copy operations
    MonotonicAllocator(const MonotonicAllocator&) = delete;
    MonotonicAllocator& operator=(const MonotonicAllocator&) = delete;
    
    // Move operations
    MonotonicAllocator(MonotonicAllocator&& other) noexcept 
        : m_buffer(other.m_buffer)
        , m_current(other.m_current)
        , m_block_size(other.m_block_size)
    {
        other.m_buffer = nullptr;
        other.m_current = nullptr;
    }
    
    MonotonicAllocator& operator=(MonotonicAllocator&& other) noexcept {
        if (this != &other) {
            // Clean up existing resources
            while (m_buffer) {
                auto buf = m_buffer;
                m_buffer = m_buffer->next;
                delete buf;
            }
            
            m_buffer = other.m_buffer;
            m_current = other.m_current;
            m_block_size = other.m_block_size;
            
            other.m_buffer = nullptr;
            other.m_current = nullptr;
        }
        return *this;
    }

    [[nodiscard]] char* allocate(size_t bytes, size_t alignment) {
        assert(bytes > 0 && "Cannot allocate zero bytes");
        assert((alignment & (alignment - 1)) == 0 && "Alignment must be power of 2");
        
        if (bytes > m_block_size) {
            throw std::bad_alloc();
        }

        auto available = m_current->size - (reinterpret_cast<std::byte*>(m_current->pos) - m_current->buf);
        
        if (std::align(alignment, bytes, m_current->pos, available)) {
            auto tmp = reinterpret_cast<char*>(m_current->pos);
            m_current->pos = reinterpret_cast<std::byte*>(m_current->pos) + bytes;
            return tmp;
        }
        
        // Need a new block - allocate dynamically
        if (m_current->next != nullptr) {
            m_current = m_current->next;
        } else {
            // Allocate new owned block
            m_current->next = new storage(m_block_size);
            m_current = m_current->next;
        }
        
        available = m_current->size;
        if (!std::align(alignment, bytes, m_current->pos, available)) {
            throw std::bad_alloc(); // Should not happen since we checked bytes <= m_block_size
        }
        
        auto tmp = reinterpret_cast<char*>(m_current->pos);
        m_current->pos = reinterpret_cast<std::byte*>(m_current->pos) + bytes;
        return tmp;
    }

    void deallocate([[maybe_unused]] char* p, [[maybe_unused]] std::size_t n) noexcept {
        // Arena allocator doesn't deallocate individual allocations
    }

    [[nodiscard]] constexpr std::size_t spaceNeeded(std::size_t size, size_t alignment) const noexcept {
        return (size + (alignment - 1)) & ~(alignment - 1);
    }

    [[nodiscard]] size_t availableBytesInCurrentBlock() const noexcept {
        return m_current->size - (reinterpret_cast<std::byte*>(m_current->pos) - m_current->buf);
    }

    [[nodiscard]] size_t allocatedBlocks() const noexcept {
        size_t i = 0;
        for (auto buf = m_buffer; buf; buf = buf->next) {
            ++i;
        }
        return i;
    }
    
    [[nodiscard]] size_t dynamicallyAllocatedBlocks() const noexcept {
        size_t i = 0;
        for (auto buf = m_buffer; buf; buf = buf->next) {
            if (buf->is_owned) {
                ++i;
            }
        }
        return i;
    }
    
    [[nodiscard]] bool usedExternalBufferOnly() const noexcept {
        return m_buffer != nullptr && !m_buffer->is_owned && m_buffer->next == nullptr;
    }
    
    [[nodiscard]] size_t blockSize() const noexcept {
        return m_block_size;
    }

    template<typename T, typename... Args>
    [[nodiscard]] T* construct(Args&&... args) {
        static_assert(std::alignment_of_v<T> % 2 == 0, "Type alignment must be even");
        
        if (sizeof(T) >= m_block_size) {
            throw std::bad_alloc();
        }
        
        auto tmp = allocate(sizeof(T), alignof(T));
        
        if constexpr (std::is_trivially_constructible_v<T, Args...> && sizeof...(Args) == 0) {
            // For POD types with no arguments, skip constructor call entirely
            return reinterpret_cast<T*>(tmp);
        } else if constexpr (std::is_trivially_constructible_v<T, Args...>) {
            // For trivially constructible types with arguments, use simple assignment
            return new (tmp) T(std::forward<Args>(args)...);
        } else {
            // For complex types, use full constructor
            return new (tmp) T{std::forward<Args>(args)...};
        }
    }
    
    template<typename T>
    [[nodiscard]] T* constructArray(size_t count) {
        static_assert(std::alignment_of_v<T> % 2 == 0, "Type alignment must be even");
        
        const size_t total_size = sizeof(T) * count;
        if (total_size > m_block_size) {
            throw std::bad_alloc();
        }
        
        auto tmp = allocate(total_size, alignof(T));
        T* array = reinterpret_cast<T*>(tmp);
        
        if constexpr (std::is_trivially_default_constructible_v<T>) {
            // For POD types, no need to call constructors
            // Can optionally zero-initialize if needed
            if constexpr (std::is_scalar_v<T>) {
                std::memset(tmp, 0, total_size);
            }
        } else {
            // For non-POD types, construct each element
            for (size_t i = 0; i < count; ++i) {
                new (&array[i]) T{};
            }
        }
        
        return array;
    }

    template<typename T>
    void destroy(T* p) noexcept {
        assert(p != nullptr && "Cannot destroy null pointer");
        
        if constexpr (!std::is_trivially_destructible_v<T>) {
            // Only call destructor for non-trivial types
            p->~T();
        }
        // For trivial types, skip destructor call entirely
        // Memory is not freed in arena allocator
    }
    
    template<typename T>
    void destroyArray(T* p, size_t count) noexcept {
        assert(p != nullptr && "Cannot destroy null pointer");
        
        if constexpr (!std::is_trivially_destructible_v<T>) {
            // Only call destructors for non-trivial types
            for (size_t i = count; i > 0; --i) {
                p[i - 1].~T();
            }
        }
        // For trivial types, skip destructor calls entirely
    }

    /*
     * Reuse internal storage.
     * WARNING: Only call this when ALL containers using this arena have been destroyed!
     * */
    void rewind() noexcept {
        m_current = m_buffer;
        // Reset all block positions
        for (auto block = m_buffer; block; block = block->next) {
            block->pos = block->buf;
        }
    }
    
    /*
     * Safer reset that deallocates extra blocks beyond the first one
     */
    void reset() noexcept {
        if (!m_buffer) return;
        
        // Keep first block, delete the rest
        auto next = m_buffer->next;
        while (next) {
            auto tmp = next;
            next = next->next;
            delete tmp;
        }
        
        m_buffer->next = nullptr;
        m_buffer->pos = m_buffer->buf;
        m_current = m_buffer;
    }
};

/*
 * A stateful allocator to be used with std::vector/deque only.
 * */
template <class T>
class BlockAllocator {
private:
    MonotonicAllocator* m_alloc;

public:
    using value_type = T;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;

    BlockAllocator(const BlockAllocator& p) = default;
    BlockAllocator(BlockAllocator&&) noexcept = default;
    BlockAllocator& operator=(const BlockAllocator&) = default;

    explicit BlockAllocator(MonotonicAllocator& a) noexcept : m_alloc(&a) {
        static_assert(std::alignment_of_v<T> % 2 == 0, "Type alignment must be even");
    }

    template <class U>
    BlockAllocator(const BlockAllocator<U>& p) noexcept : m_alloc(p.m_alloc) {
    }

    template <class U> 
    struct rebind {
        using other = BlockAllocator<U>;
    };

    [[nodiscard]] T* allocate(std::size_t n) {
        assert(n > 0 && "Cannot allocate zero elements");
        
        //Allocate using our custom alloc if we can serve otherwise fallback to global alloc
        if (m_alloc->spaceNeeded(sizeof(T) * n, alignof(T)) <= m_alloc->blockSize()) {
            return reinterpret_cast<T*>(m_alloc->allocate(sizeof(T) * n, alignof(T)));
        } else {
            return static_cast<T*>(::operator new(sizeof(T) * n));
        }
    }

    void deallocate(T* p, std::size_t n) noexcept {
        assert(p != nullptr && "Cannot deallocate null pointer");
        
        if (m_alloc->spaceNeeded(sizeof(T) * n, alignof(T)) > m_alloc->blockSize()) {
            ::operator delete(p);
        }
    }

    template <class A, class B>
    friend bool operator==(const BlockAllocator<A>& x, const BlockAllocator<B>& y) noexcept;

    template <class U> friend class BlockAllocator;
};

template <class A, class B>
[[nodiscard]] inline bool operator==(const BlockAllocator<A>& x, const BlockAllocator<B>& y) noexcept {
    return &x.m_alloc == &y.m_alloc;
}

template <class A, class B>
[[nodiscard]] inline bool operator!=(const BlockAllocator<A>& x, const BlockAllocator<B>& y) noexcept {
    return !(x == y);
}

/*
 * A stateful allocator with the ability to reuse memory. Only to be used with node based containers(list,map,unordered_map etc)
 * */
template <class T>
class PoolAllocator {
private:
    MonotonicAllocator* m_alloc;
    std::vector<T*, BlockAllocator<T*>> m_pool;

public:
    using value_type = T;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;

    PoolAllocator(const PoolAllocator& p) = default;
    PoolAllocator(PoolAllocator&&) noexcept = default;
    PoolAllocator& operator=(const PoolAllocator&) = default;

    explicit PoolAllocator(MonotonicAllocator& a) noexcept : m_alloc(&a), m_pool(BlockAllocator<T*>(a)) {
        static_assert(std::alignment_of_v<T> % 2 == 0, "Type alignment must be even");
    }

    template <class U>
    PoolAllocator(const PoolAllocator<U>& p) noexcept : m_alloc(p.m_alloc), m_pool(BlockAllocator<T*>(*p.m_alloc)) {
    }

    template <class U> 
    struct rebind {
        using other = PoolAllocator<U>;
    };

    [[nodiscard]] PoolAllocator select_on_container_copy_construction() const {
        PoolAllocator tmp(*this);
        tmp.m_pool.clear(); // so we dont reuse same memory twice!
        return tmp;
    }

    /* Users aren't supposed to use this method directly but if you insist on doing it, you
     * should use it ONLY with 'placement new', we cast raw bytes or a previous object in use.
     * YOU HAVE BEEN WARNED!
     */
    [[nodiscard]] T* allocate(std::size_t n) {
        assert(n > 0 && "Cannot allocate zero elements");
        
        if (n == 1) {
            if (m_pool.empty()) {
                return reinterpret_cast<T*>(m_alloc->allocate(sizeof(T), alignof(T)));
            } else {
                auto top = m_pool.back();
                m_pool.pop_back();
                return top;
            }
        } else { // we only have this to service unordered_map internal bucket array...
            //Allocate using our custom alloc if we can serve otherwise fallback to global alloc
            if (m_alloc->spaceNeeded(sizeof(T) * n, alignof(T)) <= m_alloc->blockSize()) {
                return reinterpret_cast<T*>(m_alloc->allocate(sizeof(T) * n, alignof(T)));
            } else {
                return static_cast<T*>(::operator new(sizeof(T) * n));
            }
        }
    }

    void deallocate(T* p, std::size_t n) noexcept {
        assert(p != nullptr && "Cannot deallocate null pointer");
        
        if (n == 1) {
            m_pool.push_back(p);
        } else {
            if (m_alloc->spaceNeeded(sizeof(T) * n, alignof(T)) > m_alloc->blockSize()) {
                ::operator delete(p);
            }
        }
    }

    template <class A, class B>
    friend bool operator==(const PoolAllocator<A>& x, const PoolAllocator<B>& y) noexcept;

    template <class U> friend class PoolAllocator;

    [[nodiscard]] size_t poolSize() const noexcept {
        return m_pool.size();
    }
};

template <class A, class B>
[[nodiscard]] inline bool operator==(const PoolAllocator<A>& x, const PoolAllocator<B>& y) noexcept {
    return &x.m_alloc == &y.m_alloc;
}

template <class A, class B>
[[nodiscard]] inline bool operator!=(const PoolAllocator<A>& x, const PoolAllocator<B>& y) noexcept {
    return !(x == y);
}
