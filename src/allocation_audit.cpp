// Linked only into test_unit: audit C++ allocation during prepared kernel calls.
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>

static std::atomic<bool> audit_enabled{false};
static std::atomic<std::uint64_t> allocation_count{0};

void allocation_audit_begin()
{
    allocation_count.store(0);
    audit_enabled.store(true);
}

std::uint64_t allocation_audit_end()
{
    audit_enabled.store(false);
    return allocation_count.load();
}

void *operator new(std::size_t bytes)
{
    if (audit_enabled.load()) allocation_count.fetch_add(1);
    if (void *memory = std::malloc(bytes == 0 ? 1 : bytes)) return memory;
    throw std::bad_alloc();
}

void *operator new[](std::size_t bytes)
{
    return ::operator new(bytes);
}

void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }
