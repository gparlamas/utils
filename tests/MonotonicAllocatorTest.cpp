#include <catch2/catch_all.hpp>
#include <vector>
#include <list>
#include <string>
#include "../MonotonicAllocator.h"

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

// ============================================================================
// MonotonicAllocator Constructor Tests
// ============================================================================

TEST_CASE("MonotonicAllocator::Constructor - Default", "[constructor]") {
    MonotonicAllocator alloc;
    REQUIRE(alloc.isValid());
    REQUIRE(alloc.blockSize() == MonotonicAllocator::DefaultBlockSize);
}

TEST_CASE("MonotonicAllocator::Constructor - Custom Size", "[constructor]") {
    MonotonicAllocator alloc(8192);
    REQUIRE(alloc.isValid());
    REQUIRE(alloc.blockSize() == 8192);
}

TEST_CASE("MonotonicAllocator::Constructor - Zero Size defaults", "[constructor]") {
    MonotonicAllocator alloc(0);
    REQUIRE(alloc.isValid());
    REQUIRE(alloc.blockSize() == MonotonicAllocator::DefaultBlockSize);
}

TEST_CASE("MonotonicAllocator::Constructor - External Buffer", "[constructor]") {
    std::byte buffer[1024];
    MonotonicAllocator alloc(buffer, sizeof(buffer), 2048);
    REQUIRE(alloc.isValid());
    REQUIRE(alloc.usedExternalBufferOnly());
    REQUIRE(alloc.blockSize() == 2048);
}

TEST_CASE("MonotonicAllocator::Constructor - Invalid External Buffer (null)", "[constructor]") {
    MonotonicAllocator alloc(nullptr, 1024);
    REQUIRE(!alloc.isValid());
}

TEST_CASE("MonotonicAllocator::Constructor - Invalid External Buffer (zero size)", "[constructor]") {
    MonotonicAllocator alloc(nullptr, 0);
    REQUIRE(!alloc.isValid());
}

// ============================================================================
// MonotonicAllocator Allocation Tests
// ============================================================================

TEST_CASE("MonotonicAllocator::Allocate - Basic", "[allocate]") {
    MonotonicAllocator alloc(1024);
    char* ptr = alloc.allocate(64, 1);
    REQUIRE(ptr != nullptr);
}

TEST_CASE("MonotonicAllocator::Allocate - Zero Bytes", "[allocate]") {
    MonotonicAllocator alloc(1024);
    char* ptr = alloc.allocate(0, 1);
    REQUIRE(ptr == nullptr);
}

TEST_CASE("MonotonicAllocator::Allocate - Invalid Alignment (not power of 2)", "[allocate]") {
    MonotonicAllocator alloc(1024);
    char* ptr = alloc.allocate(64, 3);
    REQUIRE(ptr == nullptr);
}

TEST_CASE("MonotonicAllocator::Allocate - Zero Alignment", "[allocate]") {
    MonotonicAllocator alloc(1024);
    char* ptr = alloc.allocate(64, 0);
    REQUIRE(ptr == nullptr);
}

TEST_CASE("MonotonicAllocator::Allocate - Size Too Large", "[allocate]") {
    MonotonicAllocator alloc(1024);
    char* ptr = alloc.allocate(2048, 1);
    REQUIRE(ptr == nullptr);
}

TEST_CASE("MonotonicAllocator::Allocate - Alignment Respected (align 1)", "[allocate]") {
    MonotonicAllocator alloc(4096);
    char* ptr = alloc.allocate(10, 1);
    REQUIRE(ptr != nullptr);
}

TEST_CASE("MonotonicAllocator::Allocate - Alignment Respected (align 16)", "[allocate]") {
    MonotonicAllocator alloc(4096);
    char* ptr = alloc.allocate(10, 16);
    REQUIRE(ptr != nullptr);
    size_t addr = reinterpret_cast<size_t>(ptr);
    REQUIRE((addr % 16) == 0);
}

TEST_CASE("MonotonicAllocator::Allocate - Alignment Respected (align 64)", "[allocate]") {
    MonotonicAllocator alloc(8192);
    char* ptr = alloc.allocate(10, 64);
    REQUIRE(ptr != nullptr);
    size_t addr = reinterpret_cast<size_t>(ptr);
    REQUIRE((addr % 64) == 0);
}

TEST_CASE("MonotonicAllocator::Allocate - Multiple Blocks", "[allocate]") {
    MonotonicAllocator alloc(256);
    
    std::vector<char*> ptrs;
    for (int i = 0; i < 5; ++i) {
        char* ptr = alloc.allocate(200, 1);
        REQUIRE(ptr != nullptr);
        ptrs.push_back(ptr);
    }
    
    REQUIRE(alloc.allocatedBlocks() > 1);
}

TEST_CASE("MonotonicAllocator::Allocate - Available Space Decreases", "[allocate]") {
    MonotonicAllocator alloc(1024);
    size_t available_before = alloc.availableBytesInCurrentBlock();
    
    alloc.allocate(256, 1);
    size_t available_after = alloc.availableBytesInCurrentBlock();
    
    REQUIRE(available_before > available_after);
    REQUIRE(available_before == 1024);
}

TEST_CASE("MonotonicAllocator::Allocate - Uninitialized Allocator Returns Null", "[allocate]") {
    MonotonicAllocator alloc(nullptr, 0);
    char* ptr = alloc.allocate(64, 1);
    REQUIRE(ptr == nullptr);
}

// ============================================================================
// MonotonicAllocator Construction Tests
// ============================================================================

TEST_CASE("MonotonicAllocator::Construct - POD Type", "[construct]") {
    MonotonicAllocator alloc(1024);
    SimpleType* obj = alloc.construct<SimpleType>();
    
    REQUIRE(obj != nullptr);
    REQUIRE(obj->x == 0);
    REQUIRE(obj->y == 0);
}

TEST_CASE("MonotonicAllocator::Construct - POD Type Too Large", "[construct]") {
    MonotonicAllocator alloc(64);
    SimpleType* obj = alloc.construct<SimpleType>();
    
    REQUIRE(obj == nullptr);
}

TEST_CASE("MonotonicAllocator::Construct - Complex Type", "[construct]") {
    MonotonicAllocator alloc(2048);
    ComplexType* obj = alloc.construct<ComplexType>(42);
    
    REQUIRE(obj != nullptr);
    REQUIRE(obj->value == 42);
}

TEST_CASE("MonotonicAllocator::Construct - Complex Type Default Constructor", "[construct]") {
    MonotonicAllocator alloc(2048);
    ComplexType* obj = alloc.construct<ComplexType>();
    
    REQUIRE(obj != nullptr);
    REQUIRE(obj->value == 0);
}

TEST_CASE("MonotonicAllocator::ConstructArray - POD Type", "[construct-array]") {
    MonotonicAllocator alloc(4096);
    SimpleType* arr = alloc.constructArray<SimpleType>(10);
    
    REQUIRE(arr != nullptr);
    REQUIRE(arr[0].x == 0);
    REQUIRE(arr[9].y == 0);
}

TEST_CASE("MonotonicAllocator::ConstructArray - Complex Type", "[construct-array]") {
    MonotonicAllocator alloc(8192);
    ComplexType* arr = alloc.constructArray<ComplexType>(5);
    
    REQUIRE(arr != nullptr);
    REQUIRE(arr[0].value == 0);
    REQUIRE(arr[4].value == 0);
}

TEST_CASE("MonotonicAllocator::ConstructArray - Zero Count", "[construct-array]") {
    MonotonicAllocator alloc(1024);
    SimpleType* arr = alloc.constructArray<SimpleType>(0);
    REQUIRE(arr == nullptr);
}

TEST_CASE("MonotonicAllocator::ConstructArray - Too Large", "[construct-array]") {
    MonotonicAllocator alloc(256);
    SimpleType* arr = alloc.constructArray<SimpleType>(100);
    REQUIRE(arr == nullptr);
}

// ============================================================================
// MonotonicAllocator Destruction Tests
// ============================================================================

TEST_CASE("MonotonicAllocator::Destroy - POD Type", "[destroy]") {
    MonotonicAllocator alloc(1024);
    SimpleType* obj = alloc.construct<SimpleType>();
    alloc.destroy(obj);
    // Should not throw or crash
}

TEST_CASE("MonotonicAllocator::Destroy - Null Pointer", "[destroy]") {
    MonotonicAllocator alloc(1024);
    alloc.destroy<SimpleType>(nullptr);
    // Should not throw or crash
}

TEST_CASE("MonotonicAllocator::DestroyArray - POD Type", "[destroy]") {
    MonotonicAllocator alloc(4096);
    SimpleType* arr = alloc.constructArray<SimpleType>(10);
    alloc.destroyArray(arr, 10);
    // Should not throw or crash
}

TEST_CASE("MonotonicAllocator::DestroyArray - Null Pointer", "[destroy]") {
    MonotonicAllocator alloc(1024);
    alloc.destroyArray<SimpleType>(nullptr, 10);
    // Should not throw or crash
}

// ============================================================================
// MonotonicAllocator Rewind and Reset Tests
// ============================================================================

TEST_CASE("MonotonicAllocator::Rewind - Restores Space", "[rewind]") {
    MonotonicAllocator alloc(1024);
    size_t before = alloc.availableBytesInCurrentBlock();
    
    alloc.allocate(256, 1);
    size_t after_alloc = alloc.availableBytesInCurrentBlock();
    
    alloc.rewind();
    size_t after_rewind = alloc.availableBytesInCurrentBlock();
    
    REQUIRE(before > after_alloc);
    REQUIRE(after_rewind == before);
}

TEST_CASE("MonotonicAllocator::Reset - Keeps Single Block", "[reset]") {
    MonotonicAllocator alloc(256);
    
    // Force creation of multiple blocks
    for (int i = 0; i < 3; ++i) {
        alloc.allocate(200, 1);
    }
    
    size_t blocks_before = alloc.allocatedBlocks();
    alloc.reset();
    size_t blocks_after = alloc.allocatedBlocks();
    
    REQUIRE(blocks_before > 1);
    REQUIRE(blocks_after == 1);
}

TEST_CASE("MonotonicAllocator::Reset - Resets Position", "[reset]") {
    MonotonicAllocator alloc(1024);
    
    alloc.allocate(256, 1);
    size_t available_after_alloc = alloc.availableBytesInCurrentBlock();
    
    alloc.reset();
    size_t available_after_reset = alloc.availableBytesInCurrentBlock();
    
    REQUIRE(available_after_reset == 1024);
    REQUIRE(available_after_reset > available_after_alloc);
}

// ============================================================================
// MonotonicAllocator Move Semantics Tests
// ============================================================================

TEST_CASE("MonotonicAllocator::Move Constructor", "[move]") {
    MonotonicAllocator alloc1(1024);
    char* ptr1 = alloc1.allocate(64, 1);
    REQUIRE(ptr1 != nullptr);
    
    MonotonicAllocator alloc2 = std::move(alloc1);
    
    REQUIRE(alloc2.isValid());
    REQUIRE(!alloc1.isValid());
}

TEST_CASE("MonotonicAllocator::Move Assignment", "[move]") {
    MonotonicAllocator alloc1(1024);
    MonotonicAllocator alloc2(2048);
    
    alloc1.allocate(64, 1);
    
    alloc2 = std::move(alloc1);
    
    REQUIRE(alloc2.blockSize() == 1024);
    REQUIRE(!alloc1.isValid());
}

// ============================================================================
// BlockAllocator Tests
// ============================================================================

TEST_CASE("BlockAllocator::Default Constructor", "[block-allocator]") {
    BlockAllocator<int> alloc;
    int* ptr = alloc.allocate(10);
    REQUIRE(ptr == nullptr);
}

TEST_CASE("BlockAllocator::With MonotonicAllocator", "[block-allocator]") {
    MonotonicAllocator mono(4096);
    BlockAllocator<int> alloc(mono);
    
    int* ptr = alloc.allocate(10);
    REQUIRE(ptr != nullptr);
    
    alloc.deallocate(ptr, 10);
}

TEST_CASE("BlockAllocator::With std::vector", "[block-allocator]") {
    MonotonicAllocator mono(8192);
    std::vector<int, BlockAllocator<int>> vec(BlockAllocator<int>(mono));
    
    for (int i = 0; i < 100; ++i) {
        vec.push_back(i);
    }
    
    REQUIRE(vec.size() == 100);
    REQUIRE(vec[50] == 50);
}

TEST_CASE("BlockAllocator::Zero Allocation", "[block-allocator]") {
    MonotonicAllocator mono(1024);
    BlockAllocator<int> alloc(mono);
    
    int* ptr = alloc.allocate(0);
    REQUIRE(ptr == nullptr);
}

TEST_CASE("BlockAllocator::Deallocate Null", "[block-allocator]") {
    MonotonicAllocator mono(1024);
    BlockAllocator<int> alloc(mono);
    
    alloc.deallocate(nullptr, 10);
    // Should not crash
}

TEST_CASE("BlockAllocator::Allocator Comparison - Same Monotonic", "[block-allocator]") {
    MonotonicAllocator mono(1024);
    BlockAllocator<int> alloc1(mono);
    BlockAllocator<int> alloc2(mono);
    
    REQUIRE(alloc1 == alloc2);
    REQUIRE(!(alloc1 != alloc2));
}

TEST_CASE("BlockAllocator::Allocator Comparison - Different Types", "[block-allocator]") {
    MonotonicAllocator mono(1024);
    BlockAllocator<int> alloc1(mono);
    BlockAllocator<double> alloc2(mono);
    
    REQUIRE(alloc1 == alloc2);
}

// ============================================================================
// PoolAllocator Tests
// ============================================================================

TEST_CASE("PoolAllocator::Default Constructor", "[pool-allocator]") {
    PoolAllocator<int> alloc;
    int* ptr = alloc.allocate(1);
    REQUIRE(ptr == nullptr);
}

TEST_CASE("PoolAllocator::With MonotonicAllocator", "[pool-allocator]") {
    MonotonicAllocator mono(4096);
    PoolAllocator<int> alloc(mono);
    
    int* ptr = alloc.allocate(1);
    REQUIRE(ptr != nullptr);
    
    alloc.deallocate(ptr, 1);
}

TEST_CASE("PoolAllocator::Reuse Mechanism", "[pool-allocator]") {
    MonotonicAllocator mono(4096);
    PoolAllocator<int> alloc(mono);
    
    int* ptr1 = alloc.allocate(1);
    int* saved_ptr1 = ptr1;
    alloc.deallocate(ptr1, 1);
    
    REQUIRE(alloc.poolSize() == 1);
    
    int* ptr2 = alloc.allocate(1);
    REQUIRE(ptr2 == saved_ptr1);
    REQUIRE(alloc.poolSize() == 0);
}

TEST_CASE("PoolAllocator::With std::list", "[pool-allocator]") {
    MonotonicAllocator mono(8192);
    std::list<int, PoolAllocator<int>> list(PoolAllocator<int>(mono));
    
    for (int i = 0; i < 50; ++i) {
        list.push_back(i);
    }
    
    REQUIRE(list.size() == 50);
    
    int sum = 0;
    for (auto val : list) {
        sum += val;
    }
    REQUIRE(sum == (49 * 50 / 2));
}

TEST_CASE("PoolAllocator::Array Allocation Not Pooled", "[pool-allocator]") {
    MonotonicAllocator mono(4096);
    PoolAllocator<int> alloc(mono);
    
    int* arr = alloc.allocate(10);
    REQUIRE(arr != nullptr);
    
    alloc.deallocate(arr, 10);
    REQUIRE(alloc.poolSize() == 0);
}

TEST_CASE("PoolAllocator::Deallocate Null", "[pool-allocator]") {
    MonotonicAllocator mono(1024);
    PoolAllocator<int> alloc(mono);
    
    alloc.deallocate(nullptr, 1);
    // Should not crash
}

TEST_CASE("PoolAllocator::Copy Construction Clears Pool", "[pool-allocator]") {
    MonotonicAllocator mono(4096);
    PoolAllocator<int> alloc1(mono);
    
    int* ptr1 = alloc1.allocate(1);
    alloc1.deallocate(ptr1, 1);
    
    REQUIRE(alloc1.poolSize() == 1);
    
    auto alloc2 = alloc1.select_on_container_copy_construction();
    REQUIRE(alloc2.poolSize() == 0);
}

TEST_CASE("PoolAllocator::Allocator Comparison", "[pool-allocator]") {
    MonotonicAllocator mono(1024);
    PoolAllocator<int> alloc1(mono);
    PoolAllocator<int> alloc2(mono);
    
    REQUIRE(alloc1 == alloc2);
    REQUIRE(!(alloc1 != alloc2));
}

// ============================================================================
// Edge Cases and Special Scenarios
// ============================================================================

TEST_CASE("Edge Case::External Buffer Exhaustion", "[edge-case]") {
    std::byte buffer[512];
    MonotonicAllocator alloc(buffer, sizeof(buffer), 512);
    
    char* ptr1 = alloc.allocate(256, 1);
    REQUIRE(ptr1 != nullptr);
    
    char* ptr2 = alloc.allocate(256, 1);
    REQUIRE(ptr2 != nullptr);
    
    char* ptr3 = alloc.allocate(256, 1);
    REQUIRE(ptr3 != nullptr);
    
    REQUIRE(alloc.allocatedBlocks() > 1);
}

TEST_CASE("Edge Case::Space Calculation", "[edge-case]") {
    MonotonicAllocator alloc(1024);
    
    SECTION("Align 1") {
        REQUIRE(alloc.spaceNeeded(10, 1) == 10);
    }
    
    SECTION("Align 16") {
        REQUIRE(alloc.spaceNeeded(10, 16) == 16);
        REQUIRE(alloc.spaceNeeded(16, 16) == 16);
    }
    
    SECTION("Align 64") {
        REQUIRE(alloc.spaceNeeded(33, 64) == 64);
        REQUIRE(alloc.spaceNeeded(64, 64) == 64);
    }
}

TEST_CASE("Edge Case::Dynamically Allocated Blocks Count", "[edge-case]") {
    MonotonicAllocator alloc(256);
    REQUIRE(alloc.dynamicallyAllocatedBlocks() == 1);
    
    alloc.allocate(200, 1);
    alloc.allocate(200, 1);
    
    REQUIRE(alloc.dynamicallyAllocatedBlocks() > 1);
}

TEST_CASE("Edge Case::External Buffer Only Flag", "[edge-case]") {
    std::byte buffer[1024];
    MonotonicAllocator alloc(buffer, sizeof(buffer));
    
    REQUIRE(alloc.usedExternalBufferOnly());
    
    // Force dynamic allocation
    alloc.allocate(900, 1);
    alloc.allocate(900, 1);
    
    REQUIRE(!alloc.usedExternalBufferOnly());
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST_CASE("Stress::Many Allocations", "[stress]") {
    MonotonicAllocator alloc(16384);
    std::vector<char*> ptrs;
    
    for (int i = 0; i < 100; ++i) {
        char* ptr = alloc.allocate(64, 1);
        REQUIRE(ptr != nullptr);
        ptrs.push_back(ptr);
    }
    
    REQUIRE(ptrs.size() == 100);
}

TEST_CASE("Stress::Various Alignments", "[stress]") {
    MonotonicAllocator alloc(16384);
    
    for (int align = 1; align <= 256; align *= 2) {
        char* ptr = alloc.allocate(32, align);
        REQUIRE(ptr != nullptr);
        
        size_t addr = reinterpret_cast<size_t>(ptr);
        REQUIRE((addr % align) == 0);
    }
}

TEST_CASE("Stress::Mixed Operations", "[stress]") {
    MonotonicAllocator alloc(8192);
    
    auto obj1 = alloc.construct<SimpleType>();
    auto arr1 = alloc.constructArray<SimpleType>(10);
    auto obj2 = alloc.construct<SimpleType>();
    auto arr2 = alloc.constructArray<SimpleType>(5);
    
    REQUIRE(obj1 != nullptr);
    REQUIRE(arr1 != nullptr);
    REQUIRE(obj2 != nullptr);
    REQUIRE(arr2 != nullptr);
    
    alloc.destroy(obj1);
    alloc.destroyArray(arr1, 10);
    alloc.destroy(obj2);
    alloc.destroyArray(arr2, 5);
}

TEST_CASE("Stress::Alternating Allocations and Deallocations", "[stress]") {
    MonotonicAllocator mono(4096);
    PoolAllocator<int> alloc(mono);
    
    for (int round = 0; round < 5; ++round) {
        std::vector<int*> ptrs;
        for (int i = 0; i < 20; ++i) {
            int* ptr = alloc.allocate(1);
            REQUIRE(ptr != nullptr);
            ptrs.push_back(ptr);
        }
        
        for (auto ptr : ptrs) {
            alloc.deallocate(ptr, 1);
        }
    }
    
    REQUIRE(alloc.poolSize() == 20);
}

TEST_CASE("Stress::Large Array Construction", "[stress]") {
    MonotonicAllocator alloc(65536);
    
    SimpleType* arr = alloc.constructArray<SimpleType>(500);
    REQUIRE(arr != nullptr);
    
    for (int i = 0; i < 500; ++i) {
        REQUIRE(arr[i].x == 0);
        REQUIRE(arr[i].y == 0);
    }
}
