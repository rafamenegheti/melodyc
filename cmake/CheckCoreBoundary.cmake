# A regra da secao 3.1 do PLANO.md, verificada pela maquina em vez de pela boa
# vontade: nada em core/ pode conhecer o JUCE. E o que mantem o DSP testavel num
# binario de console que sobe em milissegundos -- e um unico #include descuidado
# destroi isso em silencio, porque o codigo continua compilando.

file(GLOB_RECURSE CORE_FILES "${CORE_DIR}/*.h" "${CORE_DIR}/*.cpp")

set(OFFENDERS "")
foreach(f ${CORE_FILES})
  file(READ "${f}" contents)
  if(contents MATCHES "JuceHeader\\.h" OR contents MATCHES "#include[ \t]*<juce_")
    file(RELATIVE_PATH rel "${CORE_DIR}" "${f}")
    list(APPEND OFFENDERS "core/${rel}")
  endif()
endforeach()

list(LENGTH OFFENDERS n)
if(n GREATER 0)
  string(REPLACE ";" "\n    " pretty "${OFFENDERS}")
  message(FATAL_ERROR
    "core/ inclui JUCE -- a fronteira da secao 3.1 do plano foi quebrada:\n    ${pretty}\n"
    "Mova o codigo que precisa do JUCE para plugin/ ou plugin/ui/.")
endif()
