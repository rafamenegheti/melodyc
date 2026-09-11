/*
  ==============================================================================

    Alloc.h -- o contador de alocacao.

    "Nao aloca no thread de audio" e a unica afirmacao deste projeto que nao da
    para verificar lendo o codigo: um std::vector que cresce dentro de um ramo
    raro passa por seis revisoes sem ninguem ver. Entao a suite mede.

    O operator new global e substituido em tests/main.cpp -- uma unica unidade
    de traducao, porque duas definicoes do mesmo simbolo nao linkam. O contador
    e thread_local para que um caso que use threads (a troca de padrao sem
    trava) meca so o proprio thread.

  ==============================================================================
*/

#pragma once

#include <cstddef>

namespace test
{

inline thread_local bool countingAllocations = false;
inline thread_local int  allocationCount = 0;

/** Liga a contagem enquanto viver. Use dentro do escopo que representa um
    bloco de audio -- nunca em volta do prepare, que aloca de proposito. */
struct NoAllocations
{
    NoAllocations() noexcept
    {
        allocationCount = 0;
        countingAllocations = true;
    }

    ~NoAllocations() noexcept { countingAllocations = false; }

    int count() const noexcept { return allocationCount; }

    NoAllocations (const NoAllocations&) = delete;
    NoAllocations& operator= (const NoAllocations&) = delete;
};

} // namespace test
