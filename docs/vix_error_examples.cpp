/*
    File: vix_error_examples.cpp
    Purpose:
        This file contains commented C++ code snippets that intentionally
        trigger common C++ errors and runtime issues.

        Each section demonstrates ONE specific error that Vix CLI is able
        to detect and explain.

        ⚠️ Do NOT compile this file as-is.
        Uncomment ONE section at a time to test error handling.
*/

#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include <thread>

// ============================================================================
// [CPP001] cout not declared (missing <iostream> / std::)
// ============================================================================

/*
int main()
{
    cout << "Hello"; // error: 'cout' is not declared
}
*/

// ============================================================================
// [CPP002] header not found
// ============================================================================

/*
#include "missing.hpp" // fatal error: file not found
int main() {}
*/

// ============================================================================
// [CPP003] missing semicolon
// ============================================================================

/*
int main()
{
    int x = 42  // error: expected ';'
    std::cout << x << "\n";
}
*/

// ============================================================================
// [CPP004] streaming std::vector to ostream
// ============================================================================

/*
int main()
{
    std::vector<int> v{1, 2, 3};
    std::cout << v << "\n"; // error: invalid operands to operator<<
}
*/

// ============================================================================
// [CPP005] overload ambiguity with nullptr
// ============================================================================

/*
void process(int) {}
template <typename T>
void process(T*) {}

int main()
{
    process(nullptr); // error: no matching function
}
*/

// ============================================================================
// [CPP006] use-after-move
// ============================================================================

/*
struct Obj { void run() {} };

int main()
{
    auto ptr = std::make_unique<Obj>();
    auto other = std::move(ptr);
    ptr->run(); // error: use-after-move
}
*/

// ============================================================================
// [MEM001] unique_ptr copy (ownership violation)
// ============================================================================

/*
int main()
{
    std::unique_ptr<int> a = std::make_unique<int>(42);
    auto b = a; // error: use of deleted copy constructor
}
*/

// ============================================================================
// [MEM002] shared_ptr raw pointer double delete
// ============================================================================

/*
int main()
{
    int* p = new int(5);
    std::shared_ptr<int> a(p);
    std::shared_ptr<int> b(p); // runtime double delete
}
*/

// ============================================================================
// [MEM003] delete vs delete[]
// ============================================================================

/*
int main()
{
    int* arr = new int[10];
    delete arr; // mismatched delete
}
*/

// ============================================================================
// [MEM004] dangling std::string_view
// ============================================================================

/*
int main()
{
    std::string_view sv = std::string("hello");
    std::cout << sv << "\n"; // dangling view
}
*/

// ============================================================================
// [MEM005] returning reference to local variable
// ============================================================================

/*
const std::string& bad()
{
    std::string s = "hello";
    return s; // dangling reference
}
*/

// ============================================================================
// [MEM006] use of uninitialized variable
// ============================================================================

/*
int main()
{
    int x;
    std::cout << x << "\n"; // uninitialized read
}
*/

// ============================================================================
// [RT001] double free / invalid free
// ============================================================================

/*
int main()
{
    int* p = (int*)malloc(sizeof(int));
    free(p);
    free(p); // double free
}
*/

// ============================================================================
// [RT002] heap-buffer-overflow
// ============================================================================

/*
int main()
{
    int* a = new int[3];
    a[3] = 42; // heap-buffer-overflow
}
*/

// ============================================================================
// [RT003] stack-buffer-overflow
// ============================================================================

/*
int main()
{
    char buf[4];
    buf[10] = 'x'; // stack-buffer-overflow
}
*/

// ============================================================================
// [RT004] use-after-free
// ============================================================================

/*
int main()
{
    int* p = new int(5);
    delete p;
    std::cout << *p << "\n"; // use-after-free
}
*/

// ============================================================================
// [UB001] signed integer overflow (UBSan)
// ============================================================================

/*
int main()
{
    int x = 2147483647;
    x += 1; // signed overflow
}
*/

// ============================================================================
// [UB002] null pointer dereference
// ============================================================================

/*
struct Node { int v; };

int main()
{
    Node* n = nullptr;
    std::cout << n->v << "\n"; // UB
}
*/

// ============================================================================
// [TS001] data race (ThreadSanitizer)
// ============================================================================

/*
int shared = 0;

void worker()
{
    for (int i = 0; i < 1000; ++i)
        shared++;
}

int main()
{
    std::thread t1(worker);
    std::thread t2(worker);
    t1.join();
    t2.join();
}
*/

// ============================================================================
// End of file
// ============================================================================
