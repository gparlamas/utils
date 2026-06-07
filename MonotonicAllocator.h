#pragma once

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>
#include <cassert>
#include <cstring>

// ============================================================================
// Debug Sanitizer Support
// ============================================================================

#ifdef NDEBUG
    #define ALLOCATOR_DEBUG_MODE 0
#else
    #define ALLOCATOR_DEBUG_MODE 1
#endif

// Try to detect and include ASAN interface
#if ALLOCATOR_DEBUG_MODE
    #ifdef __has_include
        #if __has_include(<sanitizer/asan_interface.h>)
            #include <sanitizer/asan_interface.h>
            #define ALLOCATOR_ASAN_ENABLED 1
        #else
            #define ALLOCATOR_ASAN_ENABLED 0
        #endif
    #elif defined(__SANITIZER_INTERFACE_H__)
        #include <sanitizer/asan_interface.h>
        #define ALLOCATOR_ASAN_ENABLED 1
    #else
        #define ALLOCATOR_ASAN_ENABLED 0
    #endif
#else
    #define ALLOCATOR_ASAN_ENABLED 0
#endif

#if ALLOCATOR_ASAN_ENABLED && ALLOCATOR_DEBUG_MODE
    #define POISON_MEMORY(addr, size) ASAN_POISON_MEMORY_REGION((addr), (size))
    #define UNPOISON_MEMORY(addr, size) ASAN_UNPOISON_MEMORY_REGION((addr), (size))
    #define MARK_ALLOCATED(addr, size) UNPOISON_MEMORY((addr), (size))
    #define MARK_FREED(addr, size) POISON_MEMORY((addr), (size))
#else
    #define POISON_MEMORY(addr, size) do { } while (0)
    #define UNPOISON_MEMORY(addr, size) do { } while (0)
    #define MARK_ALLOCATED(addr, size) do { } while (0)
    #define MARK_FREED(addr, size) do { } while (0)
#endif

/*
 * Arena style allocator that can use a pre-allocated buffer or allocate dynamically.
 * Can be initialized with an external buffer for zero-allocation scenarios.
 * Only allocates additional blocks when the provided buffer is exhausted.
 * It releases only dynamically allocated memory on destruction.
 * 
 * Thread-safety: NOT thread-safe. Use external synchronization if needed.
 * Returns nullptr on allocation failure instead of throwing exceptions.
 * Debug mode with ASAN: Includes memory safety checks via AddressSanitizer.
 * Compiler support: GCC and Clang with -fsanitize=address flag.
 * */
class MonotonicAllocator {
public:
    static constexpr size_t DefaultBlockSize = 4080;

private:
    struct storage {
        std::unique_ptr<std::byte[]> owned_buf;  // Only set if we own the memory
        std::byte* buf;                          // Points to either owned or external buffer
        size_t size;
        std::byte* pos = nullptr;                // Changed from void* for type safety
        storage* next = nullptr;
        bool is_owned;
        
        // Constructor for owned (allocated) buffer
        explicit storage(size_t block_size) 
            : owned_buf(std::make_unique<std::byte[]>(block_size))
            , buf(owned_buf.get())
            , size(block_size)
            , pos(buf)
            , is_owned(true)
        {
            #if ALLOCATOR_ASAN_ENABLED && ALLOCATOR_DEBUG_MODE
            // Poison the entire buffer initially
            POISON_MEMORY(buf, block_size);
            pos = buf;
            #endif
        }
        
        // Constructor for external (non-owned) buffer
        storage(std::byte* external_buf, size_t buffer_size) noexcept
            : owned_buf(nullptr)
            , buf(external_buf)
            , size(buffer_size)
            , pos(buf)
            , is_owned(false)
        {
            #if ALLOCATOR_ASAN_ENABLED && ALLOCATOR_DEBUG_MODE
            // Poison the external buffer region
            POISON_MEMORY(buf, buffer_size);
            pos = buf;
            #endif
        }
        
        // Disable copy, allow move
        storage(const storage&) = delete;
        storage& operator=(const storage&) = delete;
        storage(storage&&) noexcept = default;
        storage& operator=(storage&&) noexcept = default;
    };

    storage* m_buffer = nullptr;
    storage* m_current = nullptr;
    size_t m_block_size;

    // Helper to safely calculate remaining bytes in current block
    [[nodiscard]] size_t getAvailableBytes() const noexcept {
        assert(m_current != nullptr);
        assert(m_current->pos >= m_current->buf);
        assert(m_current->pos <= m_current->buf + m_current->size);
        return m_current->size - (m_current->pos - m_current->buf);
    }

public:
    ~MonotonicAllocator() noexcept {
        while (m_buffer) {
            auto buf = m_buffer;
            m_buffer = m_buffer->next;
            
            #if ALLOCATOR_ASAN_ENABLED && ALLOCATOR_DEBUG_MODE
            if (buf->is_owned) {
                // Unpoison before freeing
                UNPOISON_MEMORY(buf->buf, buf->size);
            }
            #endif
            
            delete buf;
        }
    }

    // Constructor with external buffer (zero-allocation mode)
    explicit MonotonicAllocator(void* buffer, size_t buffer_size, size_t overflow_block_size = DefaultBlockSize) noexcept
        : m_block_size(normalizeBlockSize(overflow_block_size))
    {
        if (buffer == nullptr || buffer_size == 0) {
            return; // Invalid parameters - allocator remains uninitialized
        }
        
        try {
            m_buffer = new storage(static_cast<std::byte*>(buffer), buffer_size);
            m_current = m_buffer;
        } catch (...) {
            m_buffer = nullptr;
            m_current = nullptr;
        }
    }
    
    // Constructor that allocates its own buffer
    explicit MonotonicAllocator(size_t block_size = DefaultBlockSize) noexcept
        : m_block_size(normalizeBlockSize(block_size))
    {
        try {
            m_buffer = new storage(m_block_size);
            m_current = m_buffer;
        } catch (...) {
            m_buffer = nullptr;
            m_current = nullptr;
        }
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
                
                #if ALLOCATOR_ASAN_ENABLED && ALLOCATOR_DEBUG_MODE
                if (buf->is_owned) {
                    UNPOISON_MEMORY(buf->buf, buf->size);
                }
                #endif
                
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

    // Check if allocator is properly initialized
    [[nodiscard]] bool isValid() const noexcept {
        return m_buffer != nullptr && m_current != nullptr;
    }

    [[nodiscard]] char* allocate(size_t bytes, size_t alignment) noexcept {
        if (bytes == 0 || m_current == nullptr) {
            return nullptr;
        }
        if ((alignment & (alignment - 1)) != 0 || alignment == 0) {
            return nullptr; // Invalid alignment
        }
        
        if (bytes > m_block_size) {
            return nullptr; // Allocation too large
        }

        // Try to allocate from current block
        auto available = getAvailableBytes();
        std::byte* aligned_pos = m_current->pos;
        
        if (std::align(alignment, bytes, reinterpret_cast<void*&>(aligned_pos), available)) {
            auto result = reinterpret_cast<char*>(aligned_pos);
            m_current->pos = aligned_pos + bytes;
            
            #if ALLOCATOR_ASAN_ENABLED && ALLOCATOR_DEBUG_MODE
            // Unpoison the newly allocated region
            MARK_ALLOCATED(result, bytes);
            #endif
            
            return result;
        }
        
        // Need a new block - allocate dynamically
        if (m_current->next != nullptr) {
            m_current = m_current->next;
        } else {
            // Try to allocate new owned block
            try {
                m_current->next = new storage(m_block_size);
                m_current = m_current->next;
            } catch (...) {
                return nullptr; // Memory allocation failed
            }
        }
        
        // Allocate from new block
        aligned_pos = m_current->pos;
        available = m_current->size;
        if (!std::align(alignment, bytes, reinterpret_cast<void*&>(aligned_pos), available)) {
            return nullptr; // Should not happen since we checked bytes <= m_block_size
        }
        
        auto result = reinterpret_cast<char*>(aligned_pos);
        m_current->pos = aligned_pos + bytes;
        
        #if ALLOCATOR_ASAN_ENABLED && ALLOCATOR_DEBUG_MODE
        // Unpoison the newly allocated region
        MARK_ALLOCATED(result, bytes);
        #endif
        
        return result;
    }

    void deallocate([[maybe_unused]] char* p, [[maybe_unused]] std::size_t n) noexcept {
        // Arena allocator doesn't deallocate individual allocations
    }

    [[nodiscard]] constexpr std::size_t spaceNeeded(std::size_t size, size_t alignment) const noexcept {
        return (size + (alignment - 1)) & ~(alignment - 1);
    }

    [[nodiscard]] size_t availableBytesInCurrentBlock() const noexcept {
        return m_current ? getAvailableBytes() : 0;
    }

    [[nodiscard]] size_t allocatedBlocks() const noexcept {
        size_t count = 0;
        for (auto buf = m_buffer; buf; buf = buf->next) {
            ++count;
        }
        return count;
    }
    
    [[nodiscard]] size_t dynamicallyAllocatedBlocks() const noexcept {
        size_t count = 0;
        for (auto buf = m_buffer; buf; buf = buf->next) {
            if (buf->is_owned) {
                ++count;
            }
        }
        return count;
    }
    
    [[nodiscard]] bool usedExternalBufferOnly() const noexcept {
        return m_buffer != nullptr && !m_buffer->is_owned && m_buffer->next == nullptr;
    }
    
    [[nodiscard]] size_t blockSize() const noexcept {
        return m_block_size;
    }

    template<typename T, typename... Args>
    [[nodiscard]] T* construct(Args&&... args) noexcept {
        validateAlignment<T>();
        
        if (sizeof(T) >= m_block_size) {
            return nullptr; // Type too large
        }
        
        auto tmp = allocate(sizeof(T), alignof(T));
        if (tmp == nullptr) {
            return nullptr;
        }
        
        if constexpr (std::is_trivially_constructible_v<T, Args...> && sizeof...(Args) == 0) {
            // For POD types with no arguments, zero-initialize
            std::memset(tmp, 0, sizeof(T));
            return reinterpret_cast<T*>(tmp);
        } else if constexpr (std::is_trivially_constructible_v<T, Args...>) {
            // For trivially constructible types with arguments, use placement new
            return new (tmp) T(std::forward<Args>(args)...);
        } else {
            // For complex types, use full constructor
            return new (tmp) T(std::forward<Args>(args)...);
        }
    }
    
    template<typename T>
    [[nodiscard]] T* constructArray(size_t count) noexcept {
        validateAlignment<T>();
        
        if (count == 0) {
            return nullptr; // Invalid count
        }
        
        const size_t total_size = sizeof(T) * count;
        if (total_size > m_block_size) {
            return nullptr; // Allocation too large
        }
        
        auto tmp = allocate(total_size, alignof(T));
        if (tmp == nullptr) {
            return nullptr;
        }
        
        T* array = reinterpret_cast<T*>(tmp);
        
        if constexpr (std::is_trivially_default_constructible_v<T>) {
            // For POD types, zero-initialize
            std::memset(tmp, 0, total_size);
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
        if (p == nullptr) {
            return; // Safe to do nothing with null pointer
        }
        
        if constexpr (!std::is_trivially_destructible_v<T>) {
            // Only call destructor for non-trivial types
            p->~T();
        }
        // For trivial types, skip destructor call entirely
        // Memory is not freed in arena allocator
    }
    
    template<typename T>
    void destroyArray(T* p, size_t count) noexcept {
        if (p == nullptr) {
            return; // Safe to do nothing with null pointer
        }
        
        if constexpr (!std::is_trivially_destructible_v<T>) {
            // Only call destructors for non-trivial types, in reverse order
            for (size_t i = count; i > 0; --i) {
                p[i - 1].~T();
            }
        }
        // For trivial types, skip destructor calls entirely
    }

    /*
     * Reuse internal storage.
     * WARNING: Only call this when ALL containers using this arena have been destroyed!
     * This invalidates all previous allocations.
     * */
    void rewind() noexcept {
        m_current = m_buffer;
        // Reset all block positions
        for (auto block = m_buffer; block; block = block->next) {
            block->pos = block->buf;
            
            #if ALLOCATOR_ASAN_ENABLED && ALLOCATOR_DEBUG_MODE
            // Poison all blocks on rewind (invalidates previous allocations)
            POISON_MEMORY(block->buf, block->size);
            #endif
        }
    }
    
    /*
     * Safer reset that deallocates extra blocks beyond the first one.
     * Keeps the first block intact for reuse.
     */
    void reset() noexcept {
        if (!m_buffer) return;
        
        // Keep first block, delete the rest
        auto next = m_buffer->next;
        while (next) {
            auto tmp = next;
            next = next->next;
            
            #if ALLOCATOR_ASAN_ENABLED && ALLOCATOR_DEBUG_MODE
            if (tmp->is_owned) {
                UNPOISON_MEMORY(tmp->buf, tmp->size);
            }
            #endif
            
            delete tmp;
        }
        
        m_buffer->next = nullptr;
        m_buffer->pos = m_buffer->buf;
        m_current = m_buffer;
        
        #if ALLOCATOR_ASAN_ENABLED && ALLOCATOR_DEBUG_MODE
        // Poison the first block on reset
        POISON_MEMORY(m_buffer->buf, m_buffer->size);
        m_buffer->pos = m_buffer->buf;
        #endif
    }

private:
    static constexpr size_t normalizeBlockSize(size_t size) noexcept {
        return (size == 0) ? DefaultBlockSize : size;
    }

    template<typename T>
    static void validateAlignment() noexcept {
        // Alignment must be a power of 2 and at least 1
        static_assert(std::alignment_of_v<T> > 0, "Type alignment must be positive");
        static_assert((std::alignment_of_v<T> & (std::alignment_of_v<T> - 1)) == 0, 
                      "Type alignment must be a power of 2");
    }
};

/*
 * A stateful allocator to be used with std::vector/deque only.
 * Thread-safety: NOT thread-safe.
 * Returns nullptr on allocation failure instead of throwing exceptions.
 * Debug mode with ASAN: Includes memory safety checks via AddressSanitizer.
 * */
template <class T>
class BlockAllocator {
private:
    MonotonicAllocator* m_alloc = nullptr;

    template <class U>
    friend class BlockAllocator;

    template <class A, class B>
    friend bool operator==(const BlockAllocator<A>&, const BlockAllocator<B>&) noexcept;

public:
    using value_type = T;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;

    BlockAllocator() = default;
    BlockAllocator(const BlockAllocator&) = default;
    BlockAllocator(BlockAllocator&&) noexcept = default;
    BlockAllocator& operator=(const BlockAllocator&) = default;
    BlockAllocator& operator=(BlockAllocator&&) noexcept = default;

    explicit BlockAllocator(MonotonicAllocator& a) noexcept : m_alloc(&a) {
        validateAlignment<T>();
    }

    template <class U>
    explicit BlockAllocator(const BlockAllocator<U>& p) noexcept : m_alloc(p.m_alloc) {
    }

    template <class U> 
    struct rebind {
        using other = BlockAllocator<U>;
    };

    [[nodiscard]] T* allocate(std::size_t n) noexcept {
        if (n == 0) {
            return nullptr;
        }
        
        if (m_alloc == nullptr) {
            return nullptr; // Not initialized
        }
        
        // Allocate using our custom alloc if we can serve, otherwise fallback to global alloc
        if (m_alloc->spaceNeeded(sizeof(T) * n, alignof(T)) <= m_alloc->blockSize()) {
            return reinterpret_cast<T*>(m_alloc->allocate(sizeof(T) * n, alignof(T)));
        } else {
            try {
                return static_cast<T*>(::operator new(sizeof(T) * n));
            } catch (...) {
                return nullptr;
            }
        }
    }

    void deallocate(T* p, std::size_t n) noexcept {
        if (p == nullptr) {
            return; // Safe to deallocate null pointer
        }
        
        if (m_alloc != nullptr && m_alloc->spaceNeeded(sizeof(T) * n, alignof(T)) > m_alloc->blockSize()) {
            ::operator delete(p);
        }
    }

private:
    template<typename U>
    static void validateAlignment() noexcept {
        // Alignment must be a power of 2 and at least 1
        static_assert(std::alignment_of_v<U> > 0, "Type alignment must be positive");
        static_assert((std::alignment_of_v<U> & (std::alignment_of_v<U> - 1)) == 0,
                      "Type alignment must be a power of 2");
    }
};

template <class A, class B>
[[nodiscard]] inline bool operator==(const BlockAllocator<A>& x, const BlockAllocator<B>& y) noexcept {
    return x.m_alloc == y.m_alloc;
}

template <class A, class B>
[[nodiscard]] inline bool operator!=(const BlockAllocator<A>& x, const BlockAllocator<B>& y) noexcept {
    return !(x == y);
}

/*
 * A stateful allocator with the ability to reuse memory. Only to be used with node-based 
 * containers (list, map, unordered_map, etc).
 * 
 * WARNING: Reused pointers from the pool are NOT validated. Ensure the allocator's
 * underlying storage remains valid while using this allocator.
 * Thread-safety: NOT thread-safe.
 * Returns nullptr on allocation failure instead of throwing exceptions.
 * Debug mode with ASAN: Includes memory safety checks via AddressSanitizer.
 * */
template <class T>
class PoolAllocator {
private:
    MonotonicAllocator* m_alloc = nullptr;
    std::vector<T*, BlockAllocator<T*>> m_pool;

    template <class U>
    friend class PoolAllocator;

    template <class A, class B>
    friend bool operator==(const PoolAllocator<A>&, const PoolAllocator<B>&) noexcept;

public:
    using value_type = T;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;

    PoolAllocator() = default;
    PoolAllocator(const PoolAllocator&) = default;
    PoolAllocator(PoolAllocator&&) noexcept = default;
    PoolAllocator& operator=(const PoolAllocator&) = default;
    PoolAllocator& operator=(PoolAllocator&&) noexcept = default;

    explicit PoolAllocator(MonotonicAllocator& a) noexcept : m_alloc(&a), m_pool(BlockAllocator<T*>(a)) {
        validateAlignment<T>();
    }

    template <class U>
    explicit PoolAllocator(const PoolAllocator<U>& p) noexcept 
        : m_alloc(p.m_alloc), m_pool(BlockAllocator<T*>(*p.m_alloc)) {
    }

    template <class U> 
    struct rebind {
        using other = PoolAllocator<U>;
    };

    [[nodiscard]] PoolAllocator select_on_container_copy_construction() const {
        PoolAllocator tmp(*this);
        tmp.m_pool.clear(); // Prevent reusing same memory twice
        return tmp;
    }

    /* 
     * Allocates a single element or an array.
     * For single elements, attempts to reuse from pool before allocating new memory.
     * Returns nullptr on allocation failure.
     */
    [[nodiscard]] T* allocate(std::size_t n) noexcept {
        if (n == 0) {
            return nullptr;
        }
        
        if (m_alloc == nullptr) {
            return nullptr; // Not initialized
        }
        
        if (n == 1) {
            // Try to reuse from pool first
            if (!m_pool.empty()) {
                auto ptr = m_pool.back();
                m_pool.pop_back();
                return ptr;
            }
            // Allocate new single element
            return reinterpret_cast<T*>(m_alloc->allocate(sizeof(T), alignof(T)));
        } else {
            // For arrays, allocate using our custom alloc if we can serve, 
            // otherwise fallback to global alloc
            if (m_alloc->spaceNeeded(sizeof(T) * n, alignof(T)) <= m_alloc->blockSize()) {
                return reinterpret_cast<T*>(m_alloc->allocate(sizeof(T) * n, alignof(T)));
            } else {
                try {
                    return static_cast<T*>(::operator new(sizeof(T) * n));
                } catch (...) {
                    return nullptr;
                }
            }
        }
    }

    void deallocate(T* p, std::size_t n) noexcept {
        if (p == nullptr) {
            return; // Safe to deallocate null pointer
        }
        
        if (n == 1) {
            // Return single element to pool for reuse
            m_pool.push_back(p);
        } else {
            // For arrays, only delete if it was allocated globally
            if (m_alloc != nullptr && m_alloc->spaceNeeded(sizeof(T) * n, alignof(T)) > m_alloc->blockSize()) {
                ::operator delete(p);
            }
        }
    }

    [[nodiscard]] size_t poolSize() const noexcept {
        return m_pool.size();
    }

private:
    template<typename U>
    static void validateAlignment() noexcept {
        // Alignment must be a power of 2 and at least 1
        static_assert(std::alignment_of_v<U> > 0, "Type alignment must be positive");
        static_assert((std::alignment_of_v<U> & (std::alignment_of_v<U> - 1)) == 0,
                      "Type alignment must be a power of 2");
    }
};

template <class A, class B>
[[nodiscard]] inline bool operator==(const PoolAllocator<A>& x, const PoolAllocator<B>& y) noexcept {
    return x.m_alloc == y.m_alloc;
}

template <class A, class B>
[[nodiscard]] inline bool operator!=(const PoolAllocator<A>& x, const PoolAllocator<B>& y) noexcept {
    return !(x == y);
}
