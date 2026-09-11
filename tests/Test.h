/*
  ==============================================================================

    Test.h -- o arnes.

    Nao existe dependencia externa de proposito. O valor deste alvo esta em
    subir em milissegundos e nao precisar de host, de plugin nem de rede; trazer
    um framework com CMake proprio custaria mais do que as cem linhas daqui, e
    cobraria esse custo em toda maquina que clonar o projeto.

    O registro e por inicializador estatico, entao um arquivo novo em
    tests/cases/ entra na suite sem ninguem editar uma lista central. Um teste
    que existe e nao roda e pior que um teste ausente: ele da a sensacao de
    cobertura sem a cobertura.

    AS MEDIDAS SAO PARTE DO ARNES, e nao utilitarios de cada caso. `maxStep` em
    especial: e a medida que pega clique. Um salto de fatia sem crossfade, um
    laco que volta sem emenda, um filtro que troca de modo no meio de um ciclo
    -- nada disso mexe no RMS nem no pico, e todos aparecem aqui como um degrau
    entre amostras vizinhas. Um teste que so olha o pico passa com o plugin
    estalando.

  ==============================================================================
*/

#pragma once

#include <cmath>
#include <cstdio>
#include <vector>

namespace test
{

//==============================================================================
struct Case { const char* name; void (*fn)(); };

inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures()               { static int f = 0;          return f; }

struct Register
{
    Register (const char* n, void (*f)()) { registry().push_back ({ n, f }); }
};

#define TEST_CASE(name)                                        \
    static void name##_fn();                                   \
    static ::test::Register name##_reg (#name, name##_fn);      \
    static void name##_fn()

//==============================================================================
inline void fail (const char* file, int line, const char* what)
{
    std::printf ("      FALHOU  %s:%d  %s\n", file, line, what);
    ++failures();
}

#define CHECK(cond)                                            \
    do { if (! (cond)) ::test::fail (__FILE__, __LINE__, #cond); } while (false)

#define CHECK_NEAR(actual, expected, tol)                                     \
    do {                                                                      \
        const double a_ = (double) (actual), e_ = (double) (expected);        \
        if (! (std::fabs (a_ - e_) <= (double) (tol)))                        \
        {                                                                     \
            char buf_[256];                                                   \
            std::snprintf (buf_, sizeof (buf_),                               \
                           "%s == %s  (%.9g vs %.9g, tolerancia %.9g)",       \
                           #actual, #expected, a_, e_, (double) (tol));       \
            ::test::fail (__FILE__, __LINE__, buf_);                          \
        }                                                                     \
    } while (false)

#define CHECK_BELOW_DB(value, ceilingDb)                                      \
    do {                                                                      \
        const double d_ = ::test::toDb ((double) (value));                    \
        if (! (d_ <= (double) (ceilingDb)))                                   \
        {                                                                     \
            char buf_[256];                                                   \
            std::snprintf (buf_, sizeof (buf_), "%s = %.2f dB, teto %.2f dB", \
                           #value, d_, (double) (ceilingDb));                 \
            ::test::fail (__FILE__, __LINE__, buf_);                          \
        }                                                                     \
    } while (false)

//==============================================================================
inline double toDb (double linear) noexcept
{
    // O piso evita -inf virar "nan" no printf e nao muda decisao nenhuma:
    // -200 dB ja e silencio por qualquer criterio deste projeto.
    return linear <= 1.0e-10 ? -200.0 : 20.0 * std::log10 (linear);
}

inline double rms (const float* x, int n) noexcept
{
    if (x == nullptr || n <= 0) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += (double) x[i] * (double) x[i];
    return std::sqrt (sum / (double) n);
}

inline double maxAbs (const float* x, int n) noexcept
{
    if (x == nullptr || n <= 0) return 0.0;
    double m = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double a = std::fabs ((double) x[i]);
        if (a > m) m = a;
    }
    return m;
}

/** A maior diferenca entre amostras vizinhas. E o detector de clique: um degrau
    aparece aqui mesmo quando o RMS e o pico continuam perfeitamente normais. */
inline double maxStep (const float* x, int n) noexcept
{
    if (x == nullptr || n < 2) return 0.0;
    double m = 0.0;
    for (int i = 1; i < n; ++i)
    {
        const double d = std::fabs ((double) x[i] - (double) x[i - 1]);
        if (d > m) m = d;
    }
    return m;
}

/** A maior diferenca amostra a amostra entre dois sinais. Zero exato e o que
    "bit-identico" quer dizer neste projeto. */
inline double maxDiff (const float* a, const float* b, int n) noexcept
{
    if (a == nullptr || b == nullptr || n <= 0) return 0.0;
    double m = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double d = std::fabs ((double) a[i] - (double) b[i]);
        if (d > m) m = d;
    }
    return m;
}

} // namespace test
