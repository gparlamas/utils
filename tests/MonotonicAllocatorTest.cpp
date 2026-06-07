#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include <list>
#include <map>
#include <string>
#include "../MonotonicAllocator.h"

// Test utilities
class TestSuite {
public:
    int passed = 0;
    int failed = 0;

    void assert_true(bool condition, const char* test_name) {
        if (condition) {
            passed++;
            std::cout << "✓ " << test_name << std::endl;
        } else {
            failed++;
            std::cout << "✗ " << test_name << std::endl;
        }
    }

    void assert_equal(const char* a, const char* b, const char* test_name) {
        if (std::strcmp(a, b) == 0) {
            passed++;
            std::cout << "✓ " << test_name << std::endl;
        } else {
            failed++;
            std::cout << "✗ " << test_name << std::endl;
        }
    }

    void print_summary() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Tests passed: " << passed << std::endl;
        std::cout << "Tests failed: " << failed << std::endl;
        std::cout << "Total: " << (passed + failed) << std::endl;
        std::cout << "========================================\n" << std::endl;
    }
};

// Test data structures
struct SimpleType {
    int x;
    int y;
};

struct ComplexType {
    int value;
    char buffer[64];
    std::string str;

    ComplexType() : value(0), str("") {
        std::memset(buffer, 0, sizeof(buffer));
    }

    ComplexType(int v) : value(v), str("test") {
        std::memset(buffer, 0, sizeof(buffer));
    }

    ~ComplexType() {
        // Cleanup
    }
};

struct NonTrivialType {
    int* ptr;

    NonTrivialType() : ptr(nullptr) {}
    
    NonTrivialType(int val) {
        ptr = new int(val);
    }

    ~NonTrivialType() {
        delete ptr;
    }
};

// ============================================================================
// MonotonicAllocator Tests
// ============================================================================

void test_constructor_default(TestSuite& suite) {
    std::cout << "\n--- Constructor Tests ---" << std::endl;
    
    MonotonicAllocator alloc;
    suite.assert_true(alloc.isValid(), "Default constructor creates valid allocator");
    suite.assert_true(alloc.blockSize() == MonotonicAllocator::DefaultBlockSize, 
                     "Default block size is set correctly");
}

void test_constructor_with_size(TestSuite& suite) {
    MonotonicAllocator alloc(8192);
    suite.assert_true(alloc.isValid(), "Constructor with size creates valid allocator");
    suite.assert_true(alloc.blockSize() == 8192, "Custom block size is set");
}

void test_constructor_with_external_buffer(TestSuite& suite) {
    std::byte buffer[1024];
    MonotonicAllocator alloc(buffer, sizeof(buffer), 2048);
    suite.assert_true(alloc.isValid(), "Constructor with external buffer is valid");
    suite.assert_true(alloc.usedExternalBufferOnly(), "External buffer is used");
    suite.assert_true(alloc.blockSize() == 2048, "Overflow block size is set");
}

void test_constructor_invalid_external_buffer(TestSuite& suite) {
    MonotonicAllocator alloc1(nullptr, 1024);
    suite.assert_true(!alloc1.isValid(), "Constructor rejects null buffer");

    MonotonicAllocator alloc2(nullptr, 0);
    suite.assert_true(!alloc2.isValid(), "Constructor rejects zero size buffer");
}

void test_allocate_basic(TestSuite& suite) {
    std::cout << "\n--- Allocation Tests ---" << std::endl;
    
    MonotonicAllocator alloc(1024);
    char* ptr = alloc.allocate(64, 1);
    suite.assert_true(ptr != nullptr, "Basic allocation succeeds");
}

void test_allocate_zero_bytes(TestSuite& suite) {
    MonotonicAllocator alloc(1024);
    char* ptr = alloc.allocate(0, 1);
    suite.assert_true(ptr == nullptr, "Allocate with zero bytes returns nullptr");
}

void test_allocate_invalid_alignment(TestSuite& suite) {
    MonotonicAllocator alloc(1024);
    char* ptr = alloc.allocate(64, 3); // Not power of 2
    suite.assert_true(ptr == nullptr, "Invalid alignment returns nullptr");
}

void test_allocate_too_large(TestSuite& suite) {
    MonotonicAllocator alloc(1024);
    char* ptr = alloc.allocate(2048, 1);
    suite.assert_true(ptr == nullptr, "Allocation larger than block returns nullptr");
}

void test_allocate_alignment(TestSuite& suite) {
    MonotonicAllocator alloc(4096);
    char* ptr1 = alloc.allocate(10, 1);
    char* ptr2 = alloc.allocate(10, 16);
    
    suite.assert_true(ptr1 != nullptr, "Allocation with align 1 succeeds");
    suite.assert_true(ptr2 != nullptr, "Allocation with align 16 succeeds");
    
    size_t addr2 = reinterpret_cast<size_t>(ptr2);
    suite.assert_true((addr2 % 16) == 0, "Alignment is respected");
}

void test_allocate_multiple_blocks(TestSuite& suite) {
    MonotonicAllocator alloc(256);
    
    std::vector<char*> ptrs;
    for (int i = 0; i < 5; ++i) {
        char* ptr = alloc.allocate(200, 1);
        suite.assert_true(ptr != nullptr, "Allocation succeeds");
        ptrs.push_back(ptr);
    }
    
    suite.assert_true(alloc.allocatedBlocks() > 1, "Multiple blocks allocated");
}

void test_available_bytes(TestSuite& suite) {
    std::cout << "\n--- Available Space Tests ---" << std::endl;
    
    MonotonicAllocator alloc(1024);
    size_t available_before = alloc.availableBytesInCurrentBlock();
    
    alloc.allocate(256, 1);
    size_t available_after = alloc.availableBytesInCurrentBlock();
    
    suite.assert_true(available_before > available_after, 
                     "Available bytes decreases after allocation");
    suite.assert_true(available_before == 1024, "Initial available space is correct");
}

void test_construct_pod(TestSuite& suite) {
    std::cout << "\n--- Construct Tests ---" << std::endl;
    
    MonotonicAllocator alloc(1024);
    SimpleType* obj = alloc.construct<SimpleType>();
    
    suite.assert_true(obj != nullptr, "POD construction succeeds");
    suite.assert_true(obj->x == 0 && obj->y == 0, "POD is zero-initialized");
}

void test_construct_pod_with_args(TestSuite& suite) {
    MonotonicAllocator alloc(1024);
    SimpleType* obj = alloc.construct<SimpleType>();
    
    obj->x = 42;
    obj->y = 100;
    
    suite.assert_true(obj->x == 42 && obj->y == 100, "POD members set correctly");
}

void test_construct_complex(TestSuite& suite) {
    MonotonicAllocator alloc(2048);
    ComplexType* obj = alloc.construct<ComplexType>(42);
    
    suite.assert_true(obj != nullptr, "Complex type construction succeeds");
    suite.assert_true(obj->value == 42, "Complex type constructor called");
}

void test_construct_array(TestSuite& suite) {
    std::cout << "\n--- Array Construction Tests ---" << std::endl;
    
    MonotonicAllocator alloc(4096);
    SimpleType* arr = alloc.constructArray<SimpleType>(10);
    
    suite.assert_true(arr != nullptr, "Array construction succeeds");
    suite.assert_true(arr[0].x == 0, "Array elements are zero-initialized");
    suite.assert_true(arr[9].y == 0, "All array elements initialized");
}

void test_construct_array_zero_count(TestSuite& suite) {
    MonotonicAllocator alloc(1024);
    SimpleType* arr = alloc.constructArray<SimpleType>(0);
    suite.assert_true(arr == nullptr, "Array with zero count returns nullptr");
}

void test_destroy_pod(TestSuite& suite) {
    std::cout << "\n--- Destroy Tests ---" << std::endl;
    
    MonotonicAllocator alloc(1024);
    SimpleType* obj = alloc.construct<SimpleType>();
    alloc.destroy(obj);
    suite.assert_true(true, "POD destruction succeeds");
}

void test_destroy_null(TestSuite& suite) {
    MonotonicAllocator alloc(1024);
    alloc.destroy<SimpleType>(nullptr);
    suite.assert_true(true, "Destroying null pointer is safe");
}

void test_destroy_array(TestSuite& suite) {
    MonotonicAllocator alloc(4096);
    SimpleType* arr = alloc.constructArray<SimpleType>(10);
    alloc.destroyArray(arr, 10);
    suite.assert_true(true, "Array destruction succeeds");
}

void test_rewind(TestSuite& suite) {
    std::cout << "\n--- Rewind Tests ---" << std::endl;
    
    MonotonicAllocator alloc(1024);
    size_t before = alloc.availableBytesInCurrentBlock();
    
    alloc.allocate(256, 1);
    size_t after_alloc = alloc.availableBytesInCurrentBlock();
    
    alloc.rewind();
    size_t after_rewind = alloc.availableBytesInCurrentBlock();
    
    suite.assert_true(before > after_alloc, "Available space decreases");
    suite.assert_true(after_rewind == before, "Rewind restores space");
}

void test_reset(TestSuite& suite) {
    std::cout << "\n--- Reset Tests ---" << std::endl;
    
    MonotonicAllocator alloc(256);
    
    // Allocate enough to create multiple blocks
    for (int i = 0; i < 3; ++i) {
        alloc.allocate(200, 1);
    }
    
    size_t blocks_before = alloc.allocatedBlocks();
    alloc.reset();
    size_t blocks_after = alloc.allocatedBlocks();
    
    suite.assert_true(blocks_before > 1, "Multiple blocks created");
    suite.assert_true(blocks_after == 1, "Reset keeps only first block");
}

void test_move_semantics(TestSuite& suite) {
    std::cout << "\n--- Move Semantics Tests ---" << std::endl;
    
    MonotonicAllocator alloc1(1024);
    char* ptr1 = alloc1.allocate(64, 1);
    suite.assert_true(ptr1 != nullptr, "Initial allocation succeeds");
    
    MonotonicAllocator alloc2 = std::move(alloc1);
    suite.assert_true(alloc2.isValid(), "Move constructor transfers state");
    suite.assert_true(!alloc1.isValid(), "Source becomes invalid");
}

void test_move_assignment(TestSuite& suite) {
    MonotonicAllocator alloc1(1024);
    MonotonicAllocator alloc2(2048);
    
    alloc1.allocate(64, 1);
    
    alloc2 = std::move(alloc1);
    suite.assert_true(alloc2.blockSize() == 1024, "Move assignment transfers block size");
}

// ============================================================================
// BlockAllocator Tests
// ============================================================================

void test_block_allocator_default(TestSuite& suite) {
    std::cout << "\n--- BlockAllocator Tests ---" << std::endl;
    
    BlockAllocator<int> alloc;
    int* ptr = alloc.allocate(0);
    suite.assert_true(ptr == nullptr, "Default BlockAllocator returns nullptr");
}

void test_block_allocator_with_monotonic(TestSuite& suite) {
    MonotonicAllocator mono(4096);
    BlockAllocator<int> alloc(mono);
    
    int* ptr = alloc.allocate(10);
    suite.assert_true(ptr != nullptr, "BlockAllocator with MonotonicAllocator succeeds");
    
    alloc.deallocate(ptr, 10);
    suite.assert_true(true, "Deallocation succeeds");
}

void test_block_allocator_with_vector(TestSuite& suite) {
    MonotonicAllocator mono(8192);
    std::vector<int, BlockAllocator<int>> vec(BlockAllocator<int>(mono));
    
    for (int i = 0; i < 100; ++i) {
        vec.push_back(i);
    }
    
    suite.assert_true(vec.size() == 100, "Vector with BlockAllocator works");
    suite.assert_true(vec[50] == 50, "Vector elements correct");
}

void test_block_allocator_zero_allocation(TestSuite& suite) {
    MonotonicAllocator mono(1024);
    BlockAllocator<int> alloc(mono);
    
    int* ptr = alloc.allocate(0);
    suite.assert_true(ptr == nullptr, "Zero allocation returns nullptr");
}

void test_block_allocator_null_dealloc(TestSuite& suite) {
    MonotonicAllocator mono(1024);
    BlockAllocator<int> alloc(mono);
    
    alloc.deallocate(nullptr, 10);
    suite.assert_true(true, "Deallocating null pointer is safe");
}

void test_block_allocator_comparison(TestSuite& suite) {
    MonotonicAllocator mono(1024);
    BlockAllocator<int> alloc1(mono);
    BlockAllocator<int> alloc2(mono);
    BlockAllocator<double> alloc3(mono);
    
    suite.assert_true(alloc1 == alloc2, "Same MonotonicAllocator equals");
    suite.assert_true(alloc1 == alloc3, "Different types with same MonotonicAllocator equal");
}

// ============================================================================
// PoolAllocator Tests
// ============================================================================

void test_pool_allocator_default(TestSuite& suite) {
    std::cout << "\n--- PoolAllocator Tests ---" << std::endl;
    
    PoolAllocator<int> alloc;
    int* ptr = alloc.allocate(0);
    suite.assert_true(ptr == nullptr, "Default PoolAllocator returns nullptr");
}

void test_pool_allocator_with_monotonic(TestSuite& suite) {
    MonotonicAllocator mono(4096);
    PoolAllocator<int> alloc(mono);
    
    int* ptr = alloc.allocate(1);
    suite.assert_true(ptr != nullptr, "PoolAllocator allocation succeeds");
    
    alloc.deallocate(ptr, 1);
    suite.assert_true(alloc.poolSize() == 1, "Deallocated item added to pool");
}

void test_pool_allocator_reuse(TestSuite& suite) {
    MonotonicAllocator mono(4096);
    PoolAllocator<int> alloc(mono);
    
    int* ptr1 = alloc.allocate(1);
    int* saved_ptr1 = ptr1;
    alloc.deallocate(ptr1, 1);
    
    int* ptr2 = alloc.allocate(1);
    suite.assert_true(ptr2 == saved_ptr1, "Pool reuses deallocated pointer");
    suite.assert_true(alloc.poolSize() == 0, "Pool is now empty");
}

void test_pool_allocator_with_list(TestSuite& suite) {
    MonotonicAllocator mono(8192);
    std::list<int, PoolAllocator<int>> list(PoolAllocator<int>(mono));
    
    for (int i = 0; i < 50; ++i) {
        list.push_back(i);
    }
    
    suite.assert_true(list.size() == 50, "List with PoolAllocator works");
    
    int sum = 0;
    for (auto val : list) {
        sum += val;
    }
    suite.assert_true(sum == (49 * 50 / 2), "List values correct");
}

void test_pool_allocator_array_allocation(TestSuite& suite) {
    MonotonicAllocator mono(4096);
    PoolAllocator<int> alloc(mono);
    
    int* arr = alloc.allocate(10);
    suite.assert_true(arr != nullptr, "Array allocation succeeds");
    
    alloc.deallocate(arr, 10);
    suite.assert_true(alloc.poolSize() == 0, "Array not added to single element pool");
}

void test_pool_allocator_null_dealloc(TestSuite& suite) {
    MonotonicAllocator mono(1024);
    PoolAllocator<int> alloc(mono);
    
    alloc.deallocate(nullptr, 1);
    suite.assert_true(true, "Deallocating null pointer is safe");
}

void test_pool_allocator_copy_construction(TestSuite& suite) {
    MonotonicAllocator mono(4096);
    PoolAllocator<int> alloc1(mono);
    
    int* ptr1 = alloc1.allocate(1);
    alloc1.deallocate(ptr1, 1);
    
    auto alloc2 = alloc1.select_on_container_copy_construction();
    suite.assert_true(alloc2.poolSize() == 0, "Copied allocator has empty pool");
}

// ============================================================================
// Edge Cases and Stress Tests
// ============================================================================

void test_external_buffer_exhaustion(TestSuite& suite) {
    std::cout << "\n--- Edge Cases ---" << std::endl;
    
    std::byte buffer[512];
    MonotonicAllocator alloc(buffer, sizeof(buffer), 512);
    
    char* ptr1 = alloc.allocate(256, 1);
    suite.assert_true(ptr1 != nullptr, "First allocation from external buffer");
    
    char* ptr2 = alloc.allocate(256, 1);
    suite.assert_true(ptr2 != nullptr, "Second allocation from external buffer");
    
    // Next allocation should fail or use new block
    char* ptr3 = alloc.allocate(256, 1);
    suite.assert_true(ptr3 != nullptr, "Third allocation uses new block");
    suite.assert_true(alloc.allocatedBlocks() > 1, "New block allocated on exhaustion");
}

void test_deallocate_uninitialized(TestSuite& suite) {
    MonotonicAllocator alloc(1024);
    alloc.deallocate(nullptr, 100);
    suite.assert_true(true, "Deallocate does nothing (arena allocator)");
}

void test_space_needed_calculation(TestSuite& suite) {
    MonotonicAllocator alloc(1024);
    
    size_t space1 = alloc.spaceNeeded(10, 1);
    suite.assert_true(space1 == 10, "Space for align 1");
    
    size_t space2 = alloc.spaceNeeded(10, 16);
    suite.assert_true(space2 == 16, "Space for align 16 rounds up");
    
    size_t space3 = alloc.spaceNeeded(16, 16);
    suite.assert_true(space3 == 16, "Space for align 16 exact");
}

void test_dynamically_allocated_blocks_count(TestSuite& suite) {
    MonotonicAllocator alloc(256);
    suite.assert_true(alloc.dynamicallyAllocatedBlocks() == 1, "Initial block is owned");
    
    // Force allocation of more blocks
    alloc.allocate(200, 1);
    alloc.allocate(200, 1);
    
    suite.assert_true(alloc.dynamicallyAllocatedBlocks() > 1, "Multiple owned blocks");
}

void test_external_buffer_only_flag(TestSuite& suite) {
    std::byte buffer[1024];
    MonotonicAllocator alloc(buffer, sizeof(buffer));
    
    suite.assert_true(alloc.usedExternalBufferOnly(), "Only external buffer used");
    
    // Force dynamic allocation
    alloc.allocate(900, 1);
    alloc.allocate(900, 1);
    
    suite.assert_true(!alloc.usedExternalBufferOnly(), "Now using dynamic blocks");
}

// ============================================================================
// Stress Tests
// ============================================================================

void test_stress_many_allocations(TestSuite& suite) {
    std::cout << "\n--- Stress Tests ---" << std::endl;
    
    MonotonicAllocator alloc(16384);
    std::vector<char*> ptrs;
    
    for (int i = 0; i < 100; ++i) {
        char* ptr = alloc.allocate(64, 1);
        suite.assert_true(ptr != nullptr, "Stress allocation succeeds");
        ptrs.push_back(ptr);
    }
    
    suite.assert_true(ptrs.size() == 100, "All 100 allocations succeeded");
}

void test_stress_various_alignments(TestSuite& suite) {
    MonotonicAllocator alloc(16384);
    
    for (int align = 1; align <= 256; align *= 2) {
        char* ptr = alloc.allocate(32, align);
        suite.assert_true(ptr != nullptr, "Allocation with align succeeded");
        
        size_t addr = reinterpret_cast<size_t>(ptr);
        suite.assert_true((addr % align) == 0, "Alignment is correct");
    }
}

void test_stress_mixed_operations(TestSuite& suite) {
    MonotonicAllocator alloc(8192);
    
    // Mix of single objects and arrays
    auto obj1 = alloc.construct<SimpleType>();
    auto arr1 = alloc.constructArray<SimpleType>(10);
    auto obj2 = alloc.construct<SimpleType>();
    auto arr2 = alloc.constructArray<SimpleType>(5);
    
    suite.assert_true(obj1 != nullptr && arr1 != nullptr && 
                     obj2 != nullptr && arr2 != nullptr,
                     "Mixed operations succeed");
    
    alloc.destroy(obj1);
    alloc.destroyArray(arr1, 10);
    alloc.destroy(obj2);
    alloc.destroyArray(arr2, 5);
    
    suite.assert_true(true, "Mixed destruction succeeds");
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
    TestSuite suite;

    std::cout << "========================================" << std::endl;
    std::cout << "MonotonicAllocator Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    // MonotonicAllocator tests
    test_constructor_default(suite);
    test_constructor_with_size(suite);
    test_constructor_with_external_buffer(suite);
    test_constructor_invalid_external_buffer(suite);

    test_allocate_basic(suite);
    test_allocate_zero_bytes(suite);
    test_allocate_invalid_alignment(suite);
    test_allocate_too_large(suite);
    test_allocate_alignment(suite);
    test_allocate_multiple_blocks(suite);

    test_available_bytes(suite);

    test_construct_pod(suite);
    test_construct_pod_with_args(suite);
    test_construct_complex(suite);

    test_construct_array(suite);
    test_construct_array_zero_count(suite);

    test_destroy_pod(suite);
    test_destroy_null(suite);
    test_destroy_array(suite);

    test_rewind(suite);
    test_reset(suite);

    test_move_semantics(suite);
    test_move_assignment(suite);

    // BlockAllocator tests
    test_block_allocator_default(suite);
    test_block_allocator_with_monotonic(suite);
    test_block_allocator_with_vector(suite);
    test_block_allocator_zero_allocation(suite);
    test_block_allocator_null_dealloc(suite);
    test_block_allocator_comparison(suite);

    // PoolAllocator tests
    test_pool_allocator_default(suite);
    test_pool_allocator_with_monotonic(suite);
    test_pool_allocator_reuse(suite);
    test_pool_allocator_with_list(suite);
    test_pool_allocator_array_allocation(suite);
    test_pool_allocator_null_dealloc(suite);
    test_pool_allocator_copy_construction(suite);

    // Edge cases
    test_external_buffer_exhaustion(suite);
    test_deallocate_uninitialized(suite);
    test_space_needed_calculation(suite);
    test_dynamically_allocated_blocks_count(suite);
    test_external_buffer_only_flag(suite);

    // Stress tests
    test_stress_many_allocations(suite);
    test_stress_various_alignments(suite);
    test_stress_mixed_operations(suite);

    suite.print_summary();

    return suite.failed == 0 ? 0 : 1;
}
