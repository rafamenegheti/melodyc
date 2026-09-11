# melody

> Gera quatro compassos que soam como quatro compassos de verdade — porque são.

Plugin gerador de melodia. VST3 e AU. O gerador **não inventa a frase**: ele
escolhe uma frase real de quatro compassos, minerada de 2.062 MIDIs de kits, e a
transpõe para o tom pedido.

## Por que assim

Três versões anteriores inventaram a frase e as três falharam. A explicação de
cada uma está no cabeçalho de [core/Generator.h](core/Generator.h); o resumo:

| tentativa | o que fazia | o que dava |
|---|---|---|
| regras escritas à mão | passo pequeno, contorno suave, célula rítmica escolhida a dedo | "notas uma atrás da outra que nem sequer combinam" |
| estatística de 2.108 MIDIs | metade dos intervalos é salto, o ritmo mora na colcheia | números certos, som errado — distribuição agregada não captura **sequência** |
| células reais de um compasso | arranjo A-B-A'-C montado por cima | melhor, e a **forma** da frase ainda era invenção minha |
| **esta** | a frase inteira vem do arquivo | ela já traz a própria repetição, variação e resolução |

E duas decisões que valem mais que o tamanho do banco:

**As alturas são semitons acima da tônica, não graus de escala.** Transpor virou
uma soma, e a frase soa exatamente como soava no arquivo de origem. Grau de
escala reescreve o intervalo — uma terça menor vira maior conforme a escala
destino — e é assim que material real vira material genérico. O preço é ter
banco de maior e de menor separados, e o preço vale.

**Qualidade é peso de sorteio, não porteiro.** A versão anterior filtrou por
repetição interna e terminou com **24 frases embutidas**; 24 frases repetem, que
era exatamente a reclamação. Filtrar por silêncio mínimo custava 1.789 dos 3.443
trechos, e trecho legato é estilo, não defeito. Pontuando, o banco foi de 24 para
**3.274**.

## O piano roll é um piano roll

Três coisas faltavam, e as três eram perguntas que qualquer um faz olhando:

- **Que nota é essa?** Não havia teclado. Agora há, na lateral, com o dó de cada
  oitava nomeado. As teclas pretas são desenhadas por cima das brancas e mais
  curtas — do mesmo comprimento elas não formam o desenho de teclado que o olho
  reconhece, viram listras.
- **Em que compasso eu estou?** Só contando as linhas mais grossas. Agora há
  régua numerada em cima.
- **Qual é a dinâmica?** O banco guarda a velocity real dos MIDIs de origem e ela
  não aparecia em lugar nenhum: duas notas com dinâmica bem diferente desenhavam
  idênticas. Agora ela entra no brilho.

As linhas de tecla preta ficam mais escuras, que é o que permite achar uma altura
sem contar linha — o olho reconhece o padrão de duas e três pretas e sabe onde
está. E a grade marca compasso e tempo, não semicolcheia: linha por semicolcheia
neste tamanho vira textura, não informação.

### Os buracos encolhem

Medido sobre centenas de frases com `melody_smoke --span`: a janela vertical
ocupava **~40 semitons, e ~19 dessas linhas não tinham nota nenhuma.** Metade do
roll desenhava grade. A nota sobrava com 11 pixels — e por isso a velocity no
brilho, o halo e o aquecimento, todos já implementados, quase não apareciam:
faltava superfície onde aparecer.

Agora uma corrida de três ou mais linhas vazias vira uma **costura**, que vale
0,45 de linha. Buraco curto continua valendo altura cheia, porque buraco curto é
o desenho do próprio acorde. Resultado: 25,8 unidades no lugar de 39,6, e a nota
passa de 11,6 para **17,8 px (+53%)**.

O que se perde é a distância *literal* entre clusters distantes — entre o baixo e
os acordes podem passar duas oitavas de vazio. O que fica intacto é o intervalo
*dentro* de cada cluster, que é o que se lê num acorde ou numa linha. E a quebra
é declarada: o teclado da lateral é interrompido e duas diagonais atravessam a
emenda, como o eixo quebrado de um gráfico. Sem isso o dó3 e o dó4 encostariam
sem aviso, e a lateral passaria a mentir sobre a distância.

**Não era o baixo.** A primeira hipótese foi dar pista própria ao baixo, tirada de
UMA captura em que ele parecia ocupar um terço do campo. A medição matou: sem o
baixo a janela cai de 39,6 para 32,8 semitons, e a pista custaria mais do que os
7 semitons que economiza — a nota *encolheria*. O custo do campo é buraco, não
camada. É por isso que o `--span` ficou no repositório.

A quebra também não pode ser barata demais. Na primeira versão ela valia uma
linha inteira e a lateral virava um código de barras de entalhes disputando
atenção com as notas. Valendo menos da metade de uma linha, ela lê como emenda
entre dois trechos de teclado — que é o que ela é.

**O chassi é cinza dessaturado e o roll é mais escuro que ele.** É a hierarquia
do Logic, e ela tem função: o roll afundado separa conteúdo de controle sem
precisar de moldura, e as cores das notas mantêm o contraste que perderiam sobre
cinza médio. O que não foi copiado de lá são os biséis e gradientes dos botões —
é a parte datada, e brigaria com o resto.

## O piano roll é a parte viva

Duas animações, e as duas informam em vez de enfeitar.

**Desligar uma camada cala ela agora.** O note-off já saía sempre, mas saía na
hora em que a nota acabaria — e uma nota longa podia levar meio laço para isso.
Botão de silenciar que demora doze compassos para agir não parece silenciar,
parece quebrado.

**A nota acende quando soa** — mais saturada, um pouco maior, com um halo curto —
e esfria ao longo de 0,9 batida depois de acabar. Antes disso você via a linha
passar por cima de retângulos parados e não sabia o que estava tocando. O calor
é **função pura da posição do cursor**: guardar por nota quando ela acendeu daria
o mesmo desenho e um estado a mais para dessincronizar quando o transporte salta.

**A frase chega em vez de aparecer.** Apertar GERAR trocava tudo num quadro só, e
por isso não contava nada — tinha acabado de acontecer uma escolha entre todo o
banco e a janela dava a mesma resposta de um redesenho qualquer. Agora as notas
nascem estreitas e abrem até a largura de verdade, escalonadas por **posição no
compasso** e não por índice na lista: por índice as camadas chegariam em blocos
(melodia inteira, depois acordes) em vez de o compasso se montar da esquerda para
a direita. São 420 ms, com `easeOutCubic` para a nota parecer pousar.

Duas decisões de execução que vieram do fundo claro e do fato de ser um plugin:

- **Nada de brilho aditivo.** Sobre claro ele simplesmente some. O que lê como
  "acendeu" aqui é saturação, tamanho e um halo.
- **O halo é dois retângulos translúcidos, não um `DropShadow`.** Aquele
  renderiza uma imagem borrada a cada chamada; com meia dúzia de notas quentes a
  30 quadros por segundo são centenas de borrões por segundo no thread de
  interface, disputando com a DAW inteira.

A janela só repinta enquanto há o que animar. Parada, não gasta um quadro.

## A janela

**Três faixas: o que define a frase, a frase, e o que se faz com ela.**

Em cima, os quatro controles que respondem "o que vai sair quando eu apertar
GERAR" — fonte, compassos, tom, escala. Todos visíveis, nenhum atrás de clique:
ler os quatro de relance vale mais que a linha limpa. No meio, só o piano roll,
de borda a borda. Embaixo, o `GERAR` sempre no mesmo lugar, o `TOCAR`, as três
camadas e os dois arrastar.

**O `JUNTO / SEPARADO` é o menu do próprio botão que exporta.** Um botão com duas
zonas: o corpo arrasta o arquivo, a seta escolhe como ele sai. A escolha só
importa na hora de mandar a frase para algum lugar, e é lá que ela mora — deixá-la
num controle solto do outro lado da janela era pedir para o usuário decidir antes
de saber que ia precisar.

E ela vale para os dois destinos: junto manda tudo no canal 1 e exporta uma
trilha só; separado dá um canal e uma trilha por camada. No separado a fusão de
alturas repetidas deixa de acontecer — em trilhas separadas as camadas não se
encontram, e o arquivo sai com todas as notas geradas, que é o motivo de alguém
escolher separado.

**A lista mostra só instrumentos, e decide isso sem carregar nada.** Um efeito de
áudio no slot carrega, não soa, e não há nada na janela que explique por quê — ele
não recebe nota. Carregar os 62 plugins para perguntar o tipo custaria segundos e
arriscaria a DAW num bundle quebrado; em vez disso, para Audio Unit o próprio
identificador carrega o tipo de quatro letras (`aumu` é instrumento), e para VST3
o bundle traz um `moduleinfo.json` que declara as subcategorias — basta procurar
"Instrument" no texto. O arquivo tem vírgula sobrando no fim das listas, então
parser estrito falha nele; procurar a palavra não falha. Quem não declara nada
entra na lista: esconder um plugin bom por falta de metadado é pior que mostrar um
que não serve. Na máquina de teste, 11 dos 62 passam.

**O menu é desenhado pela janela, não pelo JUCE.** O visual padrão é cinza claro
de outro aplicativo, e num plugin escuro o menu seria o único lugar onde a
interface troca de identidade — justamente quando o usuário está escolhendo
alguma coisa. Só os métodos de menu são sobrescritos; um LookAndFeel completo
obrigaria a responder por todo componente do JUCE que a janela nunca usa.

O seletor de som fica na barra de cima, junto do resto que define o resultado:
escolher o som é coisa que se faz olhando, não merecia um clique a mais.

Duas disposições foram descartadas antes desta:

- **Coluna de controles ao lado do roll** — era a do plugin de referência, e
  pior: quase tudo ali se mexe uma vez e ocupava espaço permanente ao lado da
  única coisa que se olha o tempo todo.
- **Trilho de ícones com painel deslizante** — resolvia o problema certo e
  cobrava caro: cinco glifos que ninguém lê sem passar o mouse, e um painel que
  empurrava o conteúdo a cada abertura.
- **Engrenagem com painel flutuante** — melhor, e ainda escondia duas coisas que
  não precisavam estar escondidas. Com o `JUNTO / SEPARADO` virando menu do
  arrastar e o som subindo para a barra, não sobrou nada atrás dela.

**Escuro neutro.** Quatro decisões de paleta, e as quatro têm motivo:

- **O fundo é neutro, não azul-marinho.** Preto azulado parece escolha de cor e
  puxa o olho. Num plugin cujo miolo é um piano roll colorido, a única cor da
  janela tem que ser a das notas — assim a cor volta a significar alguma coisa.
- **Superfície é branco com alfa, não um cinza fixo.** Um cinza só funciona sobre
  um fundo; branco a 6% funciona sobre qualquer um, e empilha.
- **Três níveis de texto, e só três.** 92% para o que se lê, 56% para rótulo, 34%
  para o desligado. Quando tudo pode ser um pouco mais claro, nada tem hierarquia.
- **Sem sombra.** Sobre quase preto ela vira mancha. O que separa um plano do
  outro é um fio de um pixel.

A fonte é a do sistema, e as camadas usam as cores de sistema no escuro.

**O custo assumido:** a paleta clara existia para o melody parecer irmão do
Plasma, do Lume e do mosaic. Saindo dela, ele deixa de parecer da família.

## Compilar

Sem Projucer. O CMake usa o `~/JUCE` local se existir e baixa a versão travada
se não.

```bash
cmake -S ~/projetos/melody -B ~/projetos/melody/build -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

```bash
cmake --build ~/projetos/melody/build -j8
```

> **O build já instala o plugin.** `COPY_PLUGIN_AFTER_BUILD` está ligado, então o
> VST3 e o AU vão para `~/Library/Audio/Plug-Ins/` a cada compilação. Se um host
> insistir em mostrar versão velha, é cache de scan — não é o build.

Universal para distribuir: `-DMELODY_UNIVERSAL=ON`.

## O instrumento mora dentro

O slot `SOM` carrega o **seu** VST3 ou Audio Unit dentro do melody. Uma faixa, um
plugin: a frase é gerada aqui e sai tocada pelo instrumento que você escolheu.

Sem isso, usar o melody com o próprio som exigia rotear MIDI entre duas faixas —
trabalho de engenheiro, e a primeira coisa que faz alguém desistir de um plugin.

**O instrumento ocupa a janela inteira, com uma barra de VOLTAR em cima.** Foram
três formas até esta:

1. **Janela solta.** No Live as janelas de plugin ficam sempre por cima, então as
   duas brigavam pelo topo e não havia como trazer o instrumento de volta.
2. **Embutido ao lado dos controles.** A janela precisava caber os dois, e um
   instrumento de 1.180 pixels empurrava o melody para 1.430 de largura, com
   rolagem horizontal por causa de controle que ninguém estava olhando.
3. **Tela cheia com VOLTAR.** A janela vira o tamanho do instrumento e mais nada;
   os controles do melody somem enquanto ele está aberto, porque as duas coisas
   não são olhadas ao mesmo tempo. O que não couber na tela rola dentro de um
   `Viewport`.

Três regras que o código segue e que não são óbvias:

- **O thread de áudio nunca espera pela troca.** Carregar um plugin leva de
  centenas de milissegundos a segundos. O áudio usa `tryEnter`: se a troca está em
  curso, o bloco sai mudo. Um bloco mudo na troca é barato; um estouro de prazo no
  meio de uma gravação, não.
- **O convidado processa num buffer dele.** Ele pode declarar oito canais quando a
  DAW nos deu dois, e entregar o nosso seria leitura fora do lugar — num plugin
  isso é a DAW caindo, não uma mensagem de erro.
- **O editor do convidado é uma view nativa, e foto por software não pega.** Por
  isso o `--rack` mede geometria em vez de olhar pixel: que a janela cresceu, que
  o editor virou filho dela e que tem tamanho. Uma captura mostraria um retângulo
  preto e pareceria defeito do encaixe.
- **A troca de instrumento é observada por contador.** Carregar um preset na DAW
  chama `setStateInformation`, que descarrega e recarrega o convidado sem passar
  pela janela — o editor embutido ficaria apontando para um plugin já destruído.
- **A entrada MIDI vai para o convidado, e só para ele.** Sem isso você carrega um
  piano dentro do melody e não consegue tocar uma nota nele com a mão. Já a saída
  MIDI para o resto do projeto leva só as notas geradas — repassar a entrada faria
  a frase gravada trazer junto tudo o que você dedilhou procurando um som.

**O identificador de plugin é uma string, não um caminho.** Para VST3 os dois
coincidem; para Audio Unit, não — AU é identificado pelo registro do sistema
(`AudioUnit:Synths/aumu,dls ,appl`). A primeira versão procurava por caminho e o
slot listava os AU e recusava todos eles.

```bash
~/projetos/melody/build/melody_smoke_artefacts/RelWithDebInfo/melody_smoke --rack "AudioUnit:Synths/aumu,dls ,appl"
```

O `--rack` carrega um instrumento de verdade no slot e diz se a nota gerada aqui
virou áudio lá dentro. Não dá para deixar isso na suíte — qual plugin existe muda
de máquina — mas a conta só fecha com um instrumento real.

## Duas versões, o mesmo código

| | formato | onde entra | som |
|---|---|---|---|
| **melody** | VST3 + AU (`aumu`) | uma faixa, com o instrumento dentro | slot de instrumento, ou som interno |
| **melody FX** | só AU (`aumi`) | slot de MIDI FX, antes do instrumento | nenhum — quem toca é o instrumento da faixa |

Com o slot funcionando, a versão FX virou o caminho secundário: ela existe para
quem prefere a cadeia do Logic à hospedagem. Em qualquer outra DAW, use o
**melody** e carregue o instrumento dentro.

**Por que a FX é só AU.** `melody_smoke --load` carrega os bundles instalados num
host de verdade e mostra o que cada formato virou:

```
VST3       melody FX  ->  categoria "Fx", 0 entradas, 0 saídas, efeito midi NÃO
AudioUnit  melody FX  ->  categoria "MidiEffects",              efeito midi SIM
```

O AU tem o tipo `aumi`, que é um conceito real de host. O VST3 não tem
equivalente: sai como efeito de **áudio** com zero barramentos, o host tenta
abrir como efeito de áudio e falha — foi um "This VST3 plug-in could not be
opened" no Ableton.

**Uma ressalva sobre AU dentro de AU.** O Logic roda AU em caixa de areia, e
carregar um AU dentro de outro pode ser barrado por lá. No Logic, prefira o VST3
do melody, ou a versão FX.

## Duas fontes de tempo, e o botão sabe disso

O plugin toca por dois motivos diferentes: o **transporte do host** e o botão
`TOCAR`. O botão olhava só o segundo, e isso dava dois defeitos que pareciam
distintos e eram o mesmo:

- Com o projeto rodando, o botão continuava escrito TOCAR — apertar não parava
  nada, porque não havia audição interna para parar.
- Quem apertou TOCAR antes de rodar o projeto ficava com as duas ligadas. Ao
  apertar espaço para parar, a audição interna assumia e o som continuava, sem
  nada na barra de transporte que pudesse pará-lo.

Duas mudanças. **O host ganha**: quando o transporte dele passa a mandar, a
audição interna é desligada, então espaço volta a ser a autoridade. E o rótulo
passou a ter **três estados, porque há três**: `TOCAR`, `PARAR` e `HOST` — com o
projeto rodando o botão não pode parar nada, então ele diz isso em vez de mentir.

O rótulo é escrito num lugar só, derivado do estado. Escrever no clique e no
timer deixava os dois discordarem, que era a outra metade do problema.

## Quatro ou oito compassos

O interruptor `4 COMPASSOS / 8 COMPASSOS` troca de banco, e **não** cola dois
trechos de quatro. Colar seria inventar a forma da frase por cima de material
emprestado — o erro da terceira tentativa, descrito no cabeçalho de
[core/Generator.h](core/Generator.h). O corpus tem 1.486 arquivos de oito
compassos contra 453 de quatro: o material existe inteiro e não precisa ser
montado.

O banco de oito passa por dois filtros que o de quatro não tem, e os dois saíram
de medição:

| descartado | quantos | por quê |
|---|---|---|
| oito que é quatro repetido | 624 | a segunda metade é cópia exata da primeira em todas as camadas — o banco de quatro já tem esse material, do mesmo arquivo |
| oito com metade vazia | 370 | menos de três notas depois do compasso 4: são quatro compassos com cauda |

Sobram **801 trechos de oito** contra 3.274 de quatro. A comparação ignora a
velocity de propósito: quatro compassos escritos duas vezes com a dinâmica um
pouco diferente ainda são quatro compassos escritos duas vezes — com a velocity
na conta, 126 em 800 passavam.

A recombinação só cruza comprimentos iguais. Melodia de oito sobre harmonia de
quatro deixaria a segunda metade sem acorde nenhum, e o gerador não repete a
harmonia para tapar o buraco — repetir seria inventar a forma.

```bash
~/projetos/melody/build/melody_smoke_artefacts/RelWithDebInfo/melody_smoke --render /tmp/oito.wav 8 20250908 8
```

## Arrastar: MIDI ou WAV

`ARRASTAR MIDI` entrega a frase como notas. `ARRASTAR WAV` entrega como áudio,
**renderizado pelo instrumento carregado** quando há um — o arquivo tem que soar
como o que se ouve, senão arrastar WAV entrega um som que não é o do projeto.
Sem instrumento no slot, usa um sintetizador próprio, e não o do áudio:
compartilhar o de tocar deixaria o render pegar vozes no meio e a audição pegar
as do arquivo.

O render offline pega o lock inteiro do slot, e não o `tryEnter` do áudio — aqui
esperar é o certo, porque o arquivo tem que sair completo. Enquanto dura, o
thread de áudio sai mudo: alguns milissegundos de silêncio no clique de
arrastar, que é o preço de não processar o convidado por dois threads ao mesmo
tempo. Depois o convidado leva um all-notes-off, porque o envelope dele não sabe
que aquilo era um arquivo.

O WAV sai a 44,1 kHz, 24 bits, estéreo, com dois segundos de cauda, e só é
normalizado se estourou — baixar um render que já estava bom mudaria o volume
relativo entre um arraste e o próximo.

## O arrastar é um clipe só

O `ARRASTAR MIDI` escreve **uma trilha**, formato 0, com as três camadas juntas.
Em trilhas separadas a DAW criava três faixas ao receber o arraste — o oposto de
arrastar para o instrumento que já está ali.

Numa trilha só aparece um problema que trilhas separadas escondiam: **37% das
frases têm alguma altura repetida entre camadas**, e duas notas iguais
sobrepostas no mesmo canal são um note-on sem par — o primeiro note-off encerra
as duas, e a nota mais longa é cortada no meio sem nada no piano roll
explicando. O exportador funde esse par numa nota só. Encostar não conta:
nota que começa exatamente onde a outra acaba é repique, e repique continua sendo
duas notas.

O note-off sai **um tique antes do fim** (1 em 960). Se ele caísse no mesmo tique
de um note-on da mesma altura, quem lê decide a ordem — e metade dos leitores
decide errado, comendo a nota repicada.

```bash
~/projetos/melody/build/melody_smoke_artefacts/RelWithDebInfo/melody_smoke --midi /tmp/saida.mid
```

O `--midi` deixa o arquivo no disco para ser aberto por outro leitor. O caso da
suíte confere com o leitor do próprio JUCE, que é o mesmo que escreveu — se ele
tivesse um viés, os dois teriam o mesmo. Foi abrindo com o parser de
[tools/smf.py](tools/smf.py) que apareceu um andamento errado: o arquivo saía a
140 BPM (o chute inicial) para uma frase de 160, porque o andamento só era
escrito quando o transporte rodava.

**O canal MIDI importa.** As três camadas têm canal próprio (1 melodia, 2
acordes, 3 baixo), e instrumento monotimbral costuma escutar só o canal 1 —
sairia só a melodia. Por isso o padrão é `TUDO EM 1`, e `SEPARADO` fica para quem
quer cada camada num instrumento diferente. Somar as camadas num canal só cria
uma colisão: duas camadas na mesma altura viram uma nota só, e o note-off de uma
cortaria a outra. O tocador pergunta à frase se outra camada ainda segura aquela
altura — conta pura, sem estado (`heldAt`, em [core/Player.h](core/Player.h)).

## Os seis alvos

| alvo | o que é | conhece JUCE? |
|---|---|---|
| `melody_core` | banco, gerador, tocador e sintetizador, header-only | **não, e isso é verificado** |
| `Rack` (em `plugin/`) | o slot que hospeda o instrumento do usuário | sim, é o que ele faz |
| `melody` | o plugin como instrumento: VST3 + AU | sim |
| `melody_fx` | o mesmo, como efeito MIDI (AU) | sim |
| `melody_data` | os 606 KB do banco, embutidos no binário | — |
| `melody_tests` | console que prova o core | não |
| `melody_smoke` | o processador real, sem host | sim |

`melody_core` não pode incluir JUCE. Não é convenção, é um alvo de CMake
(`melody_core_boundary`) que roda a cada build e derruba a compilação.

## Rodar os testes

```bash
~/projetos/melody/build/melody_tests
```

Quinze casos, sobe em milissegundos. Cada um verifica uma propriedade que o
módulo existe para ter: que nenhuma frase sai da faixa tocável, que transpor
preserva os intervalos, que sementes vizinhas dão frases diferentes, que gerar
não aloca, e que um blob truncado é recusado em vez de estourar.

```bash
~/projetos/melody/build/melody_smoke_artefacts/RelWithDebInfo/melody_smoke
```

Instancia o processador de verdade: nota chegando ao buffer MIDI na amostra
certa, silêncio exato com o som interno desligado, round-trip de estado **com a
semente**, o `.mid` do arrastar, e a janela que abre.

E três casos sobre **nota presa**, que foi um defeito real: `Player` não guarda
quais notas soam — só emite o note-off quando a janela de batidas *atravessa* o
fim da nota — então parar, saltar ou desligar uma camada no meio de um acorde
deixava a nota tocando para sempre. O tocador continua sem estado (é o que faz
salto de transporte não precisar de ressincronização); quem **para, salta ou dá
play** é que pede silêncio, e o note-off deixou de ficar atrás da mesma condição
do note-on.

## Ver e ouvir sem abrir a DAW

Conferir interface por descrição não funciona, e conferir melodia menos ainda.

```bash
~/projetos/melody/build/melody_smoke_artefacts/RelWithDebInfo/melody_smoke --shot /tmp/melody.png
```

```bash
~/projetos/melody/build/melody_smoke_artefacts/RelWithDebInfo/melody_smoke --render /tmp/melody.wav 12
```

O `--render` passa pelo caminho de verdade — gerador, tocador e sintetizador, os
mesmos que o plugin usa.

O terceiro argumento do `--shot` é `anim=<0..1>[,<batida>[,<semente>[,8]]]`:
congela a animação de chegada, congela o cursor, escolhe a frase e o comprimento.

```bash
melody_smoke --shot /tmp/oito.png "anim=1.0,12.5,7,8"
```

A semente existe porque conferir um layout em UMA frase não prova nada — foi
exatamente assim que a hipótese do baixo passou por boa.

```bash
melody_smoke --span 600
```

O `--span` mede o campo vertical, quantas linhas não desenham nada, e a taxa de
recombinação do MIXADO. Ele contradisse a primeira ideia de layout antes de ela
virar código, e foi ele que provou que cortar o Discover não quebrou o MIXADO.

```bash
~/projetos/melody/build/melody_smoke_artefacts/RelWithDebInfo/melody_smoke --load
```

O `--load` carrega os plugins **já instalados** num host JUCE de verdade e diz em
que categoria cada formato caiu, quantos barramentos tem e se o host o reconhece
como efeito MIDI. Existe porque erro de host não diz de quem é a culpa, e as duas
respostas possíveis — bundle quebrado, ou host recusando bundle válido — têm
consertos opostos.

## Uma origem só, e por quê

O banco tem **4.075 trechos de 1.809 arquivos**, todos de kits de MIDI
comerciais — Wavsupply e Cymatics são 90% deles. Já teve 51.004, com 46.929
vindos de uma varredura do Discover MIDI (6,74 milhões de arquivos). Esse pedaço
foi **cortado**, e o motivo não é técnico.

**Melodias de músicas conhecidas estavam lá dentro.** Um acervo de MIDI raspado
da web é, por natureza, transcrição de música lançada — é disso que as pessoas
sobem arquivo. A medição confirma, e separa os dois corpora de forma limpa:

Assinei cada melodia por intervalos e ritmo (ignora tom e oitava, então duas
transcrições da mesma música batem) e contei em quantos arquivos distintos cada
assinatura aparece:

| corpus | assinaturas em 3+ arquivos |
|---|---|
| kits comerciais | **0** |
| Discover | **615** |

Zero contra 615. Loop original escrito para produção aparece uma vez; melodia
conhecida aparece muitas, porque muita gente transcreveu. A campeã estava em
**630 arquivos diferentes**.

Dava para filtrar as repetidas — são 8,4% do Discover — e o filtro foi escrito e
medido. Ele não foi usado porque resolve o pedaço errado do problema: pega quem
foi transcrito várias vezes *naquela raspagem*, e não diz nada sobre as 34.872
assinaturas que aparecem uma vez. Nenhum filtro automático certifica que uma
melodia não é de alguém.

O que decidiu foi o contexto: enquanto era uso pessoal, o risco era do autor.
Vendendo o plugin, a melodia reconhecível sai na faixa de um comprador que não
tem como saber. Não é um risco para transferir sem avisar.

### O corte não custou o que o número sugere

| | antes (51.004) | agora (4.075) |
|---|---|---|
| frases com acordes | 89,5% | **94,8%** |
| MIXADO recombina | — | **97,3%**, encaixe médio 88% |
| `bank.bin` | 9,9 MB | 606 KB |

A cobertura de camadas melhorou, e o MIXADO continua sendo um modo: com 4.075
candidatos a recombinação ainda passa do limiar de encaixe em 97% das frases
(`melody_smoke --span`). O modo degrada com elegância por construção — sem
candidato acima do limiar em vinte tentativas, fica a harmonia que veio junto com
a melodia —, e era exatamente isso que precisava ser medido depois do corte.

`data/bank-merged.json` e `data/bank-discover.json` continuam na pasta: o corte é
uma troca de `data/bank.json`, e é reversível.

**Material novo entra pelo mesmo caminho.** `tools/mine.py` extrai, `tools/pack.py`
empacota. O corte de repetição ≥ 0,55 vale para material novo; o corpus de kits
não passa por ele, porque já foi validado por ouvido e cortá-lo por uma medida
jogaria fora coisa boa.

## O banco mora dentro do binário

`data/bank.bin` entra no plugin por `juce_add_binary_data` — não há arquivo ao
lado do `.vst3` para alguém apagar. Dá para conferir no bundle instalado:

```bash
python3 -c "d=open('$HOME/Library/Audio/Plug-Ins/VST3/melody.vst3/Contents/MacOS/melody','rb').read(); i=d.find(b'MFB2'); import struct; print(i, struct.unpack('<I', d[i+4:i+8])[0])"
```

A mágica `MFB2` aparece no offset 6.818.560 declarando 4.075 trechos, e o
`Contents/Resources` do bundle só tem o `moduleinfo.json` do VST3. O binário fica
em 13 MB, dos quais 606 KB são o banco.

**A primeira frase é sorteada.** Instância nova abre com uma melodia diferente a
cada vez; abrir sempre na mesma fazia o plugin parecer que só tinha uma, e que
não gerava nada até alguém apertar GERAR. O sorteio vale só para instância nova —
reabrir um projeto passa por `setStateInformation`, que devolve a semente
gravada, e a música que já estava escrita continua sendo a mesma. Há um caso na
suíte para cada metade disso.

## O banco

O corpus é `~/Downloads/Axel's Midi Collection` (2.393 arquivos; 331 de bateria
saem pelo nome da pasta). A cadeia é offline e roda em Python puro, sem
dependência:

```bash
python3 ~/projetos/melody/tools/mine.py && python3 ~/projetos/melody/tools/pack.py
```

O minerador aceita pasta, e mais de uma. `--dry` mede sem gravar nada — é como
avaliar um pack novo antes de adotá-lo:

```bash
python3 ~/projetos/melody/tools/mine.py ~/Downloads/PackNovo --dry
```

```bash
python3 ~/projetos/melody/tools/mine.py ~/Downloads/"Axel's Midi Collection" ~/Downloads/PackNovo
```

O número que interessa não é quantos arquivos o pack tem, é quantos **trechos**
sobram depois dos filtros. Medindo os dois maiores do corpus atual: Cymatics dá
1.096 trechos de 647 arquivos; Wavsupply, 2.589 de 1.148. Cerca de 1,7 e 2,3
trechos por arquivo — um pack que renda muito menos que isso está cheio de
material que o gerador não consegue usar.

| ferramenta | o que faz |
|---|---|
| `tools/smf.py` | leitor de Standard MIDI File, casando cada note-on com o seu note-off |
| `tools/mine.py` | acha o tom, separa melodia/baixo/harmonia e recorta os trechos → `data/bank.json` |
| `tools/pack.py` | empacota na ordem recebida → `data/bank.bin` (9,9 MB, embutido no plugin) |
| `tools/traits.py` `tools/fatia.py` | as 15 medidas e a varredura de acervo grande, em fatias independentes |
| `tools/merge.py` | junta bancos preservando a prioridade de procedência |
| `tools/gen.py` `tools/demo.py` | o mesmo gerador em Python, para experimentar sem recompilar |
| `tools/preview.py` | sintetizador de conferência, quando não se quer subir o JUCE |

Três coisas na extração importam mais que o resto, e cada uma corrigiu um
defeito ouvido:

- **A voz mais aguda, não todas as notas.** 71% dos arquivos têm baixo e melodia
  na mesma trilha; tratando tudo como uma linha só, sai uma "melodia" que pula
  do baixo para o lead.
- **A duração real**, casando note-on com note-off. "Até a próxima nota" faz tudo
  emendar — o defeito relatado foi "sem espaço".
- **O baixo é o que está embaixo.** Chamar de baixo "o que sobrou fora da linha
  de cima" derrubava voz interna — às vezes acima da melodia — no lugar do 808.

## Único ou mixado

`TRECHO INTEIRO` devolve as três camadas como estavam no arquivo: combinam por
construção, porque alguém as escreveu juntas. `MIXADO` troca a harmonia por
outra do banco, e só aceita acima de um limiar de encaixe medido nota a nota e
**ponderado pela duração** — semicolcheia fora do acorde é ornamento, mínima fora
do acorde é erro.

Medindo 4.000 pares sorteados: 72% passam, e a mediana do encaixe cruzado (0,86)
é **maior** que a do par original do mesmo arquivo (0,84). Não é acaso — a
harmonia do gênero é estática, e o achado que mais mudou o projeto anterior foi
justamente esse: a progressão mais comum, de longe, é um acorde por dois
compassos.
