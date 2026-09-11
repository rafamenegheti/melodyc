/*
  ==============================================================================

    main.cpp -- roda a suite.

    O operator new global vive aqui, e so aqui: e a unica unidade de traducao
    que pode defini-lo sem colidir. Como ele entra no link do binario inteiro,
    o contador enxerga alocacao feita DENTRO de core/ -- que e exatamente o que
    precisa ser medido.

  ==============================================================================
*/

#include <cstdio>
#include <cstdlib>
#include <new>

#include "tests/Alloc.h"
#include "tests/Test.h"

void* operator new (std::size_t size)
{
    if (test::countingAllocations)
        ++test::allocationCount;

    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;

    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

int main()
{
    auto& cases = test::registry();

    std::printf ("melody: %d casos\n\n", (int) cases.size());

    int failed = 0;

    for (const auto& c : cases)
    {
        const int before = test::failures();
        std::printf ("  %s\n", c.name);
        c.fn();

        if (test::failures() > before)
            ++failed;
    }

    std::printf ("\n%s  %d de %d casos\n",
                 failed == 0 ? "verde." : "VERMELHO:",
                 (int) cases.size() - failed, (int) cases.size());

    return failed == 0 ? 0 : 1;
}
