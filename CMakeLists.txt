# ==============================================================================
#
#   melody -- gerador de melodia. VST3 e AU.
#
#   Seis alvos, na forma do mosaic:
#
#     melody_core            biblioteca INTERFACE: core/ inteiro, header-only,
#                            C++20, ZERO JUCE.
#     melody                 o plugin como instrumento (VST3 + AU).
#     melody_fx              o mesmo, como efeito MIDI: sem audio, antes do
#                            instrumento na mesma faixa.
#     melody_tests           console, sobe em milissegundos, nao conhece JUCE.
#     melody_smoke           console COM JUCE: instancia o processador de
#                            verdade e sabe tirar foto da janela.
#     melody_core_boundary   falha o build se alguem incluir JUCE em core/.
#
#   A REGRA DO PROJETO: o gerador nao inventa frase. Ele escolhe uma frase real
#   de quatro compassos do banco minerado e a transpoe -- e transpoe SOMANDO
#   SEMITONS, nunca andando por grau de escala. Tres tentativas anteriores
#   inventaram a frase e as tres sairam genericas; a explicacao de cada uma esta
#   no cabecalho de core/Generator.h. Se essa regra cair, nenhum outro criterio
#   deste arquivo pega a queda de qualidade.
#
# ==============================================================================

cmake_minimum_required(VERSION 3.22)
project(melody VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS minimo")
  option(MELODY_UNIVERSAL "Compilar universal (arm64 + x86_64)" OFF)
  if(MELODY_UNIVERSAL)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  endif()
endif()

# ------------------------------------------------------------------------------
# JUCE: o local ganha do baixado.
# ------------------------------------------------------------------------------
set(MELODY_JUCE_DIR "$ENV{HOME}/JUCE" CACHE PATH "JUCE local")
set(MELODY_JUCE_TAG "9.0.1" CACHE STRING "Tag do JUCE quando baixado")

if(EXISTS "${MELODY_JUCE_DIR}/CMakeLists.txt")
  message(STATUS "melody: JUCE local em ${MELODY_JUCE_DIR}")
  add_subdirectory("${MELODY_JUCE_DIR}" juce_build EXCLUDE_FROM_ALL)
else()
  message(STATUS "melody: baixando JUCE ${MELODY_JUCE_TAG}")
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${MELODY_JUCE_TAG}
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# 1. core/ -- e a fronteira que o protege
# ------------------------------------------------------------------------------
add_library(melody_core INTERFACE)
target_include_directories(melody_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(melody_core INTERFACE cxx_std_20)

add_custom_target(melody_core_boundary ALL
  COMMAND ${CMAKE_COMMAND}
          -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/core
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckCoreBoundary.cmake
  COMMENT "melody: verificando que core/ nao conhece o JUCE"
  VERBATIM)

# ------------------------------------------------------------------------------
# 2. o banco, embutido no binario
#
# 422 KB de blob. Embutir e o unico jeito de o plugin funcionar numa maquina que
# nao tem a pasta do projeto -- e um arquivo de dados ao lado do .vst3 seria
# apagado pelo primeiro usuario que arrastasse so o bundle.
# ------------------------------------------------------------------------------
juce_add_binary_data(melody_data
  HEADER_NAME BankData.h
  NAMESPACE   BankData
  SOURCES     "${CMAKE_CURRENT_SOURCE_DIR}/data/bank.bin")

# ------------------------------------------------------------------------------
# 3. o plugin
#
# IS_SYNTH TRUE e NEEDS_MIDI_OUTPUT TRUE: ele nao processa audio de entrada, ele
# PRODUZ nota. Sai como instrumento na DAW, e a nota gerada tambem vai para fora
# por MIDI -- que e o que permite tocar a frase num sampler do usuario em vez do
# som interno.
# ------------------------------------------------------------------------------
# A COPIA AUTOMATICA E SO NO MAC.
#
# No Mac ela vai para ~/Library, que e do usuario. No Windows o destino e
# C:\\Program Files\\Common Files\\VST3, que exige administrador: o build compila
# tudo e falha no ultimo passo, com erro de permissao, a menos que o terminal
# tenha sido aberto como administrador. Ali o .vst3 sai da pasta de build e e
# copiado a mao.
if(APPLE)
  set(MELODY_COPY_AFTER_BUILD TRUE)
else()
  set(MELODY_COPY_AFTER_BUILD FALSE)
endif()

juce_add_plugin(melody
  COMPANY_NAME                "BeefUp808"
  BUNDLE_ID                   com.BeefUp808.melodyc
  PLUGIN_MANUFACTURER_CODE    Beef
  PLUGIN_CODE                 Mldy
  FORMATS                     VST3 AU
  PRODUCT_NAME                "melodyc"
  DESCRIPTION                 "Gerador de melodia"
  IS_SYNTH                    TRUE
  NEEDS_MIDI_INPUT            TRUE
  NEEDS_MIDI_OUTPUT           TRUE
  IS_MIDI_EFFECT              FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD     ${MELODY_COPY_AFTER_BUILD})

juce_generate_juce_header(melody)

target_sources(melody PRIVATE
  plugin/Processor.cpp
  plugin/Editor.cpp
  plugin/Rack.cpp)

target_include_directories(melody PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# HOSPEDAR: o slot de instrumento carrega o VST3 ou o AU do usuario dentro do
# plugin, e e o que faz tudo caber numa faixa so.
target_compile_definitions(melody PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_REPORT_APP_USAGE=0
  JUCE_PLUGINHOST_VST3=1
  JUCE_PLUGINHOST_AU=1)

target_link_libraries(melody
  PRIVATE melody_core melody_data juce::juce_audio_utils
  PUBLIC  juce::juce_recommended_config_flags
          juce::juce_recommended_lto_flags
          juce::juce_recommended_warning_flags)

add_dependencies(melody melody_core_boundary)

# ------------------------------------------------------------------------------
# 3b. a mesma coisa como EFEITO MIDI
#
# Duas versoes do mesmo codigo, e nao um parametro que troca de modo: o formato
# e decidido no carregamento pelo host, muito antes de qualquer parametro
# existir. `melody` e instrumento, tem barramento de audio e o som de
# conferencia; `melody FX` nao tem audio nenhum e senta ANTES do instrumento na
# mesma faixa -- que e como a frase toca com o som que o usuario ja escolheu.
#
# SO AU, E ISSO FOI MEDIDO.
#
# `melody_smoke --load` carrega os bundles instalados num host de verdade e
# mostra o que cada formato virou:
#
#   VST3       melodyc FX  ->  categoria "Fx", 0 entradas, 0 saidas, efeito midi NAO
#   AudioUnit  melodyc FX  ->  categoria "MidiEffects",              efeito midi SIM
#
# O AU tem o tipo `aumi`, que o Logic mostra no slot de MIDI FX. O VST3 nao tem
# tipo equivalente: sai como efeito de AUDIO com zero barramentos, e o host
# tenta abrir como efeito de audio e falha -- foi o "This VST3 plug-in could not
# be opened" do Ableton. Um VST3 que nao abre e pior que um VST3 ausente: ele
# aparece no navegador e gasta o tempo de quem clica.
#
# Quem nao tem slot de MIDI FX usa a versao INSTRUMENTO com o MIDI roteado para
# a faixa do instrumento. O README tem o caminho por DAW.
# ------------------------------------------------------------------------------
# FORA DO MAC ELE NAO EXISTE: e so AU (ver acima), e AU e formato da Apple. No
# Windows um juce_add_plugin sem nenhum formato valido derruba a configuracao
# inteira -- inclusive o VST3 do instrumento, que nao tem nada a ver com ele.
if(APPLE)
  juce_add_plugin(melody_fx
    COMPANY_NAME                "BeefUp808"
    BUNDLE_ID                   com.BeefUp808.melodycfx
    PLUGIN_MANUFACTURER_CODE    Beef
    PLUGIN_CODE                 Mlfx
    FORMATS                     AU
    PRODUCT_NAME                "melodyc FX"
    DESCRIPTION                 "Gerador de melodia (efeito MIDI)"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${MELODY_COPY_AFTER_BUILD})

  juce_generate_juce_header(melody_fx)

  target_sources(melody_fx PRIVATE
    plugin/Processor.cpp
    plugin/Editor.cpp
    plugin/Rack.cpp)

  target_include_directories(melody_fx PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

  # HOSPEDAR: o slot de instrumento carrega o VST3 ou o AU do usuario dentro do
  # plugin, e e o que faz tudo caber numa faixa so.
  target_compile_definitions(melody_fx PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1)

  target_link_libraries(melody_fx
    PRIVATE melody_core melody_data juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

  add_dependencies(melody_fx melody_core_boundary)
endif()

# ------------------------------------------------------------------------------
# 4. os testes
#
# O banco chega por caminho, e nao pelo BinaryData: este alvo nao conhece JUCE, e
# e exatamente por isso que ele sobe em milissegundos.
# ------------------------------------------------------------------------------
file(GLOB MELODY_TEST_CASES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/*.cpp")

option(MELODY_ASAN "Compilar os testes com AddressSanitizer" OFF)

add_executable(melody_tests tests/main.cpp ${MELODY_TEST_CASES})

if(MELODY_ASAN)
  target_compile_options(melody_tests PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer -g)
  target_link_options(melody_tests PRIVATE -fsanitize=address,undefined)
endif()

target_link_libraries(melody_tests PRIVATE melody_core)
target_include_directories(melody_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(melody_tests PRIVATE
  MELODY_BANK_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/bank.bin")
add_dependencies(melody_tests melody_core_boundary)

enable_testing()
add_test(NAME melody_tests COMMAND melody_tests)

# ------------------------------------------------------------------------------
# 5. o smoke: o processador de verdade, sem host
# ------------------------------------------------------------------------------
juce_add_console_app(melody_smoke PRODUCT_NAME "melody_smoke")
juce_generate_juce_header(melody_smoke)

target_sources(melody_smoke PRIVATE
  tests/smoke.cpp
  plugin/Processor.cpp
  plugin/Editor.cpp
  plugin/Rack.cpp)

# O smoke tambem sabe ser HOST. E o unico jeito de separar "o bundle esta
# quebrado" de "o host esta recusando um bundle valido" sem abrir a DAW -- e sao
# duas conclusoes com consertos opostos.
target_compile_definitions(melody_smoke PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_STANDALONE_APPLICATION=1
  JUCE_PLUGINHOST_VST3=1
  JUCE_PLUGINHOST_AU=1
  # So no smoke: `--shot` com convidado precisa rodar o laco de mensagens antes
  # da foto, senao o editor do convidado -- que costuma se popular em callAsync
  # ou no primeiro timer -- sai preto e parece defeito do embutir.
  JUCE_MODAL_LOOPS_PERMITTED=1)

target_include_directories(melody_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(melody_smoke
  PRIVATE melody_core melody_data juce::juce_audio_utils
  PUBLIC  juce::juce_recommended_config_flags
          juce::juce_recommended_warning_flags)

if(MELODY_ASAN)
  target_compile_options(melody_smoke PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer -g)
  target_link_options(melody_smoke PRIVATE -fsanitize=address,undefined)
endif()

add_dependencies(melody_smoke melody_core_boundary)
add_test(NAME melody_smoke COMMAND melody_smoke)
