// Playground engine test runner + allocation tracker.
// Allocation tracking: global operator new/delete are replaced for this test program only. Counting
// is per thread (thread_local flag), so allocations by unrelated threads cannot cause false alarms.
#include "UnitTest.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>

namespace hemtest
{
    thread_local bool trackThisThread = false;
    std::atomic<int> allocationCount { 0 };

    void beginTracking() { allocationCount.store (0); trackThisThread = true; }
    int endTracking() { trackThisThread = false; return allocationCount.load(); }
}

static void* countedAlloc (std::size_t size)
{
    if (hemtest::trackThisThread) hemtest::allocationCount.fetch_add (1);
    if (void* p = std::malloc (size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}

void* operator new (std::size_t size)                                   { return countedAlloc (size); }
void* operator new[] (std::size_t size)                                 { return countedAlloc (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept   { try { return countedAlloc (size); } catch (...) { return nullptr; } }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept { try { return countedAlloc (size); } catch (...) { return nullptr; } }
void operator delete (void* p) noexcept                                 { std::free (p); }
void operator delete[] (void* p) noexcept                               { std::free (p); }
void operator delete (void* p, std::size_t) noexcept                    { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept                  { std::free (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept          { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept        { std::free (p); }

int main (int argc, char** argv)
{
    // hem_pg_tests              -> every test
    // hem_pg_tests name:<a,b>   -> only the named test groups
    std::vector<std::string> wanted;
    if (argc > 1 && std::strncmp (argv[1], "name:", 5) == 0)
    {
        std::string list = argv[1] + 5;
        std::size_t pos = 0;
        while (pos <= list.size())
        {
            const auto comma = list.find (',', pos);
            wanted.push_back (list.substr (pos, comma == std::string::npos ? std::string::npos : comma - pos));
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    }
    long long passes = 0, failures = 0;
    for (auto* t : hemtest::UnitTest::registry())
    {
        if (! wanted.empty() && std::find (wanted.begin(), wanted.end(), t->getName()) == wanted.end()) continue;
        t->runAll (passes, failures);
    }
    std::printf ("HEM ENGINE TEST SUMMARY: %lld passed, %lld failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
