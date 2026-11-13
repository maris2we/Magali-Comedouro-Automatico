/*
 * ============================================================
 *   COMEDOURO AUTOMATICO - CODIGO INTEGRADO (CIRCUITO REAL)
 *   LCD/menu + RTC DS1307 (Tiny RTC) + EEPROM + servo 360
 *   >>> VERSAO COMENTADA PARA ESTUDO E MANUTENCAO <<<
 * ============================================================
 *  LIGACOES (Arduino Uno):
 *    Botao "Muda"     -> D2  (outro lado no GND, usa INPUT_PULLUP)
 *    Botao "Confirma" -> D3  (outro lado no GND, usa INPUT_PULLUP)
 *    Servo (sinal)    -> D9  (alimentacao propria, GND em comum com o Arduino)
 *    LCD I2C (0x27) e RTC DS1307 / Tiny RTC (0x68) -> SDA = A4, SCL = A5 (os dois no mesmo barramento)
 *
 *  COMO FUNCIONA:
 *    - Primeira vez (EEPROM vazia): menu Fase 1 (refeicoes) -> Fase 2 (horarios) -> Fase 3 (porcoes).
 *    - Ao fim da Fase 3 a configuracao e gravada na EEPROM.
 *    - Se faltar energia, ao religar o comedouro le a EEPROM e vai direto para a Fase 4 (contagem).
 *    - Para reconfigurar: na tela de contagem, segure o botao "Muda" por 3 segundos.
 *
 *  BIBLIOTECAS: LiquidCrystal_I2C, RTClib (Adafruit), Servo, EEPROM, Wire.
 *
 * ------------------------------------------------------------
 *  GUIA RAPIDO PARA QUEM VAI MEXER NESTE CODIGO
 * ------------------------------------------------------------
 *
 *  1) A IDEIA CENTRAL: MAQUINA DE ESTADOS
 *     O programa inteiro gira em torno de UMA variavel: faseAtual.
 *     A cada volta, o loop() olha o valor dela e executa so a funcao daquela tela.
 *     Pense num trilho de trem: faseAtual diz em que estacao estamos.
 *
 *        faseAtual = 1   -> Fase 1: escolher quantas refeicoes por dia
 *        faseAtual = 2   -> Fase 2: escolher o horario de cada refeicao
 *        faseAtual = 3   -> Fase 3: escolher quantas porcoes por refeicao
 *        faseAtual = 4   -> Fase 4: contagem regressiva + despejo da racao (fica aqui para sempre)
 *        faseAtual = 98  -> "Coringa" de confirmacao (tela "Confirma? [Sim] Nao")
 *        faseAtual = 99  -> "Coringa" de espera (mostra uma mensagem por 1 s e segue)
 *
 *     Fluxo geral:
 *
 *        Liga --> EEPROM tem config valida? --SIM--> Fase 4
 *                                           --NAO--> Fase 1 -> Fase 2 -> Fase 3 -> (grava EEPROM) -> Fase 4
 *
 *        Fase 4 --(segurar "Muda" por 3 s)--> Fase 1 (reconfigurar)
 *
 *  2) REGRA DE OURO: NUNCA USE delay() NESTE PROGRAMA
 *     delay() congela o Arduino. Enquanto ele estivesse "dormindo", os botoes
 *     nao seriam lidos e o motor nao seria parado na hora certa.
 *     Em vez disso usamos millis() (milissegundos desde que o Arduino ligou):
 *     guardamos "a hora em que algo comecou" e, a cada volta do loop, comparamos
 *     "agora - inicio > tempo desejado". Assim tudo roda "ao mesmo tempo".
 *
 *  3) MAPA DO ARQUIVO (na ordem em que aparece)
 *     - Secoes 1 a 8 ........ variaveis globais (estado, botoes, dados de cada fase)
 *     - Secao 9 ............. modulos de hardware: RTC, EEPROM e motor
 *     - setup() ............. roda UMA vez quando o Arduino liga
 *     - loop() .............. roda PARA SEMPRE e despacha para a fase atual
 *     - Leitura dos botoes .. lerClique() e atualizarBotoes() (com debounce)
 *     - Fases 1, 2, 3 e 4 ... cada uma com executarFaseN(), atualizarTelaFaseN() etc.
 *     - Coringa 98 e 99 ..... confirmacao e espera, reutilizaveis por qualquer fase
 *     - Integracao .......... lerHoraRTC(), iniciarDespejo(), despejoConcluido()
 *
 *  4) PADRAO USADO EM CADA FASE
 *     iniciarFaseN()      -> chamada UMA vez ao entrar na fase (prepara variaveis e desenha a tela)
 *     executarFaseN()     -> chamada a CADA volta do loop enquanto faseAtual == N (le botoes e decide)
 *     atualizarTelaFaseN()-> so desenha no LCD (so e chamada quando algo muda, para a tela nao piscar)
 *     concluirFaseN()     -> acao do "Sim" na confirmacao (grava o valor e segue)
 *     voltarFaseN()       -> acao do "Nao" na confirmacao (volta a editar)
 *
 *  5) RECEITAS: COMO MUDAR COISAS COMUNS
 *     - Mais (ou menos) refeicoes por dia ..... MAX_REFEICOES (e TROQUE EEPROM_MAGIC_VAL, veja armadilha A)
 *     - Mais (ou menos) porcoes maximas ....... MAX_PORCOES
 *     - Minutos de 1 em 1 (em vez de 5 em 5) .. PASSO_MINUTO = 1
 *     - Quanto de racao cai por porcao ........ TEMPO_POR_PORCAO (meça na pratica com a racao)
 *     - Servo nao para / gira ao contrario .... PULSO_PARAR e PULSO_GIRAR (secao MOTOR)
 *     - Botao registrando clique "fantasma" ... tempoDebounce (suba para 80 ou 100)
 *     - Tempo para reconfigurar ............... TEMPO_RECONFIGURAR
 *     - Horario inicial sugerido na Fase 2 .... horaTemp / minutoTemp em iniciarFase2()
 *     - Textos do LCD ......................... procure os lcd.print() (16 colunas x 2 linhas)
 *
 *     COMO CRIAR UMA NOVA FASE (exemplo: fase 5):
 *       a) crie iniciarFase5(), executarFase5() e atualizarTelaFase5();
 *       b) no loop(), adicione:  else if (faseAtual == 5) { executarFase5(); }
 *       c) em executarEspera(), adicione:  else if (faseAtual == 5) { iniciarFase5(); }
 *       d) para ir a ela vindo de outra fase: iniciarEspera("Mensagem", 5);
 *
 *     COMO PEDIR UMA CONFIRMACAO "Sim/Nao" EM QUALQUER LUGAR:
 *       pedirConfirmacao("Texto 16 chars", funcaoSeSim, funcaoSeNao);
 *       (as duas funcoes sao "void nome()" sem parametros e PRECISAM mudar faseAtual
 *        ou chamar iniciarEspera(), senao o programa fica preso na tela de confirmacao)
 *
 *  6) ARMADILHAS CONHECIDAS (leia antes de alterar!)
 *     A) Se voce mudar MAX_REFEICOES, o mapa da EEPROM muda de lugar (os minutos comecam
 *        em EEPROM_ADDR_HORAS + MAX_REFEICOES). Dados antigos gravados seriam lidos no lugar
 *        errado e ainda poderiam parecer validos. Por isso, ao mudar MAX_REFEICOES, mude tambem
 *        EEPROM_MAGIC_VAL (ex.: 0xB6): isso invalida a memoria antiga e forca reconfigurar.
 *     B) O relogio (RTC) NAO e ajustado pelo menu. Ele so e ajustado automaticamente para a
 *        hora da COMPILACAO se estiver parado (bateria fraca/removida). Para acertar a hora,
 *        compile e envie o codigo logo em seguida, ou use rtc.adjust(DateTime(...)) uma vez.
 *     C) Duas refeicoes no MESMO horario dispararao um despejo so (o segundo e ignorado).
 *     D) Se o Arduino estiver sem energia no minuto exato da refeicao, ela e perdida
 *        (nao existe "recuperar refeicao atrasada").
 *     E) A EEPROM guarda 1 byte por posicao (valores de 0 a 255). Sao suficientes para
 *        horas, minutos e quantidades, mas nao use para numeros maiores sem mudar o formato.
 *     F) A biblioteca Servo usa o Timer1 do Uno: enquanto ela estiver em uso, analogWrite()
 *        (PWM) nos pinos 9 e 10 nao funciona. Nao ligue outras coisas PWM nesses pinos.
 *     G) O LCD nao imprime acentos corretamente. Por isso os textos do display estao sem acento
 *        ("Refeicoes", "Porcoes"). Os comentarios podem ter acento, os lcd.print() nao.
 * ============================================================
 */

// ------------------------------------------------------------
// BIBLIOTECAS
// ------------------------------------------------------------
// Uma "biblioteca" e codigo pronto, escrito por outras pessoas, que usamos para
// nao reinventar a roda. O #include copia as funcoes dela para o nosso programa.
#include <EEPROM.h>             // Memoria nao-volatil (guarda a configuracao)
#include <RTClib.h>             // Relogio DS1307 (Tiny RTC)
#include <Servo.h>              // Servo 360
// ==========================================
// BIBLIOTECAS E CONFIGURAÇÃO DO DISPLAY
// ==========================================
#include <Wire.h>               // Biblioteca padrão para comunicação I2C
#include <LiquidCrystal_I2C.h>  // Biblioteca para controlar o LCD via I2C

// Cria o objeto 'lcd' no endereço 0x27, com 16 colunas e 2 linhas
// (Se o display ligar mas ficar em branco ou com quadradinhos, o endereço pode ser 0x3F.
//  Um "I2C scanner" descobre o endereço certo.)
LiquidCrystal_I2C lcd(0x27, 16, 2);


// ==========================================
// TIPOS PERSONALIZADOS
// ==========================================
// Tipo "ponteiro para função sem parâmetros" (usado pela confirmação coringa para saber o que fazer em Sim/Nao)
// Explicando: um ponteiro de função é uma variável que guarda "qual função chamar".
// Assim, a tela de confirmação é genérica: quem pede a confirmação diz qual função rodar no Sim e qual no Nao.
// Sintaxe: "AcaoConfirma" passa a ser um apelido para "void (*)()", ou seja,
// qualquer função que não recebe nada e não devolve nada (void nome()).
typedef void (*AcaoConfirma)();


// ==========================================
// 1. VARIÁVEIS GLOBAIS DE ESTADO E CONTROLE
// ==========================================
// Variáveis globais (declaradas fora de qualquer função) mantêm o valor entre uma volta e outra do loop().
// É assim que o programa "lembra" em que tela está.

// O Arduino usa 'faseAtual' como o trilho de trem para saber qual tela rodar.
// Valores: 1, 2, 3, 4 (fases normais) | 98 (confirmação) | 99 (espera)
int faseAtual = 1;

// Variável que guarda a quantidade de refeições escolhida pelo usuário
int qtdRefeicoes = 1;

// Limite máximo de refeições
// 'const' = valor fixo, não muda durante a execução. Se mudar este número, leia a ARMADILHA A no topo do arquivo.
const int MAX_REFEICOES = 5;


// ==========================================
// 2. PINOS DOS BOTÕES
// ==========================================
const int pinoBotaoMuda = 2;     // Botão que incrementa o valor da tela atual
const int pinoBotaoConfirma = 3; // Botão que confirma / avança


// ==========================================
// 3. VARIÁVEIS PARA CONTROLE DE CLIQUE (DEBOUNCE)
// ==========================================
// O QUE É DEBOUNCE? Um botão mecânico "treme" ao ser apertado: em poucos milissegundos o sinal
// oscila várias vezes entre HIGH e LOW. Sem filtro, o Arduino contaria vários cliques falsos.
// A solução: só aceitar a mudança quando o sinal ficar ESTÁVEL por um tempinho (tempoDebounce).
const unsigned long tempoDebounce = 50;  // Tempo (ms) que a leitura precisa ficar estável. Se ainda houver clique fantasma no botão real, suba para 80 ou 100

// Cada botão tem o seu trio de variáveis (estado confirmado, última leitura bruta, hora da última mudança).
// bool começa em HIGH porque, com INPUT_PULLUP, botão solto = HIGH.
bool estavelMuda = HIGH;                 // Estado confirmado (já filtrado) do botão Muda
bool leituraAntMuda = HIGH;              // Última leitura bruta do pino do botão Muda
unsigned long tMudancaMuda = 0;          // Hora em que o pino do botão Muda mudou pela última vez

bool estavelConfirma = HIGH;             // Estado confirmado (já filtrado) do botão Confirma
bool leituraAntConfirma = HIGH;          // Última leitura bruta do pino do botão Confirma
unsigned long tMudancaConfirma = 0;      // Hora em que o pino do botão Confirma mudou pela última vez

// "Avisos" de clique válido. Valem só durante UM ciclo do loop e qualquer fase pode consultá-los
// Eles são recalculados a cada volta do loop() por atualizarBotoes(). As fases só precisam
// perguntar "if (clicouMuda)" e nunca mexem diretamente nos pinos.
bool clicouMuda = false;      // true no ciclo em que o botão Muda teve um clique válido
bool clicouConfirma = false;  // true no ciclo em que o botão Confirma teve um clique válido


// ==========================================
// 4. VARIÁVEIS DO ESTADO DE ESPERA (CORINGA 99)
// ==========================================
// Usadas por iniciarEspera() / executarEspera() (ver mais abaixo).
unsigned long tempoMensagem = 0; // Guarda a hora exata em que a tela de "Salvo" apareceu
int proximaFase = 0;             // Diz para o estado Coringa (99) qual é a próxima parada


// ==========================================
// 5. VARIÁVEIS DO ESTADO DE CONFIRMAÇÃO (CORINGA 98)
// ==========================================
// Usadas por pedirConfirmacao() / executarConfirmacao() (ver mais abaixo).
char textoConfirma[17];             // Texto da linha de cima da tela Confirma? (16 caracteres + terminador '\0')
AcaoConfirma acaoSim = NULL;        // Função que será chamada se o usuário confirmar com "Sim"
AcaoConfirma acaoNao = NULL;        // Função que será chamada se o usuário marcar "Nao"
bool respostaSim = true;            // Opção marcada na tela (true = Sim, false = Nao)


// ==========================================
// 6. VARIÁVEIS DA FASE 2 (HORÁRIOS)
// ==========================================
// DADOS ENTREGUES PELA CONFIGURAÇÃO AOS OUTROS MÓDULOS (RTC, servo):
//   qtdRefeicoes      -> quantas refeições por dia (1 a MAX_REFEICOES)
//   horaRefeicao[i]   -> hora (0 a 23) da refeição i
//   minutoRefeicao[i] -> minuto (0 a 59) da refeição i
//   (i vai de 0 até qtdRefeicoes - 1; só são válidos depois que a Fase 2 termina)
//   qtdPorcoes        -> quantas porções por refeição (1 a MAX_PORCOES); válido depois que a Fase 3 termina
const int PASSO_MINUTO = 5;         // Quanto os minutos aumentam a cada clique (mude para 1 se quiser precisão total)
const int ETAPA_HORA = 0;           // Sub-etapa em que o usuário ajusta a hora
const int ETAPA_MINUTO = 1;         // Sub-etapa em que o usuário ajusta os minutos

// Vetores (arrays): uma "fileira de gavetas" numeradas a partir de 0.
// horaRefeicao[0] é a hora da 1ª refeição, horaRefeicao[1] a da 2ª, e assim por diante.
int horaRefeicao[MAX_REFEICOES];    // Hora (0 a 23) já confirmada de cada refeição
int minutoRefeicao[MAX_REFEICOES];  // Minuto (0 a 59) já confirmado de cada refeição
int indiceRefeicao = 0;             // Qual refeição está sendo configurada (0 = primeira, 1 = segunda...)
int horaTemp = 8;                   // Hora "rascunho", só é gravada no vetor se o usuário confirmar
int minutoTemp = 0;                 // Minuto "rascunho", mesma lógica da hora
int subEtapaFase2 = ETAPA_HORA;     // Sub-etapa atual dentro da Fase 2


// ==========================================
// 7. VARIÁVEIS DA FASE 3 (PORÇÕES)
// ==========================================
const int MAX_PORCOES = 5;          // Limite máximo de porções (VALOR PROVISÓRIO: ajustar na integração com o módulo do servo)
int qtdPorcoes = 1;                 // Quantidade de porções escolhida pelo usuário (dado de saída para o módulo do servo)


// ==========================================
// 8. VARIÁVEIS DA FASE 4 (CONTAGEM E DESPEJO)  
// ==========================================
// Sub-estados da Fase 4: ficar contando o tempo ou mostrar "Bom Apetite!" durante o despejo
// (É uma mini máquina de estados dentro da Fase 4.)
const int ESTADO4_CONTAGEM = 0;                       // Mostra a próxima refeição e quanto falta
const int ESTADO4_DESPEJO = 1;                        // Mostra "Bom Apetite!" enquanto o motor despeja

const unsigned long INTERVALO_LEITURA_RTC = 1000;     // De quanto em quanto tempo (ms) consulta o RTC
const unsigned long TEMPO_MIN_BOM_APETITE = 3000;     // Tempo mínimo (ms) da tela "Bom Apetite!", mesmo que o motor termine antes

int estadoFase4 = ESTADO4_CONTAGEM;                   // Sub-estado atual da Fase 4
unsigned long tUltimaLeituraRTC = 0;                  // Hora (millis) da última consulta ao RTC
unsigned long tInicioDespejo = 0;                     // Hora (millis) em que o despejo começou
int ultimoDisparoMin = -1;                            // Minuto do dia (0 a 1439) do último despejo; evita despejar duas vezes no mesmo minuto
int proxIndice = -1;                                  // Refeição mostrada na tela (-1 = nada desenhado, -2 = tela "Aguardando RTC")
int proxFaltamMin = -1;                               // Minutos que faltam mostrados na tela (para só redesenhar quando mudar)


// ==========================================
// 9. MODULOS INTEGRADOS: RTC, EEPROM E MOTOR   
// ==========================================
const unsigned long TEMPO_RECONFIGURAR = 3000;        // Segurar "Muda" por 3 s na contagem reabre a configuracao

// ---- RTC DS1307 (modulo Tiny RTC) ----
// RTC = Real Time Clock, um relógio com bateria própria que continua contando mesmo com o Arduino desligado.
// O Arduino sozinho NÃO sabe a hora do dia (millis() só conta o tempo desde que ligou), por isso precisamos dele.
RTC_DS1307 rtc;                           // Objeto do relogio (I2C)
bool rtcOk = false;                       // true quando o RTC respondeu no barramento

// Tenta iniciar o RTC. Se o relogio estiver parado (bateria acabou/foi removida), ajusta para a hora da compilacao
bool iniciarRTC() {
  if (!rtc.begin()) {                // begin() falha se o módulo não responder no I2C (fio solto, módulo ausente)
    return false;
  }
  if (!rtc.isrunning()) {            // DS1307 nao tem lostPower(): isrunning() = false significa relogio parado
    Serial.println(F("[RTC] Relogio parado! Ajustando para a hora da compilacao..."));
    // __DATE__ e __TIME__ são preenchidos pelo compilador com a data/hora em que o código foi compilado.
    // O F() guarda o texto na memória flash em vez da RAM (o Uno tem só 2 KB de RAM, então isso economiza).
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  return true;
}

// ---- EEPROM: mapa de memoria (guarda a configuracao mesmo sem energia) ----
// A EEPROM é uma memória que NÃO apaga ao desligar. Cada posição (endereço) guarda 1 byte (0 a 255).
// Mapa usado por este programa (13 bytes no total):
//   endereço 0       -> assinatura (EEPROM_MAGIC_VAL)
//   endereço 1       -> quantidade de refeições
//   endereço 2       -> quantidade de porções
//   endereços 3 a 7  -> hora de cada refeição
//   endereços 8 a 12 -> minuto de cada refeição
const byte EEPROM_MAGIC_VAL    = 0xB5;                                   // Assinatura: indica que a EEPROM ja foi gravada por ESTE programa
const int  EEPROM_ADDR_MAGIC   = 0;                                      // 0      -> assinatura
const int  EEPROM_ADDR_QTD_REF = 1;                                      // 1      -> quantidade de refeicoes
const int  EEPROM_ADDR_QTD_POR = 2;                                      // 2      -> quantidade de porcoes
const int  EEPROM_ADDR_HORAS   = 3;                                      // 3..7   -> hora de cada refeicao
const int  EEPROM_ADDR_MINUTOS = EEPROM_ADDR_HORAS + MAX_REFEICOES;      // 8..12  -> minuto de cada refeicao

// Grava a configuracao atual (chamada ao fim da Fase 3). update() so escreve se o valor mudou (poupa a EEPROM)
// (Cada posição da EEPROM aguenta cerca de 100 mil gravações; update() evita gastar à toa.)
void salvarConfigEEPROM() {
  EEPROM.update(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_VAL);
  EEPROM.update(EEPROM_ADDR_QTD_REF, qtdRefeicoes);
  EEPROM.update(EEPROM_ADDR_QTD_POR, qtdPorcoes);
  // Grava os horários de TODAS as posições do vetor (mesmo as não usadas), mantendo o mapa fixo
  for (int i = 0; i < MAX_REFEICOES; i++) {
    EEPROM.update(EEPROM_ADDR_HORAS + i, horaRefeicao[i]);
    EEPROM.update(EEPROM_ADDR_MINUTOS + i, minutoRefeicao[i]);
  }
  Serial.println(F("[EEPROM] Configuracao salva."));
}

// Le a configuracao no boot. Retorna true so se TUDO estiver valido (senao nao altera nada)
// Estratégia: lê tudo para variáveis locais, valida, e SÓ DEPOIS copia para as variáveis globais.
// Assim, se algo estiver corrompido, a RAM não fica com dados pela metade.
bool carregarConfigEEPROM() {
  if (EEPROM.read(EEPROM_ADDR_MAGIC) != EEPROM_MAGIC_VAL) return false;   // Nunca gravado (ou gravado por outro programa)
  // Uma EEPROM nova vem com todos os bytes em 255 (0xFF), então a assinatura 0xB5 não bate e o menu abre normalmente.

  int qtdR = EEPROM.read(EEPROM_ADDR_QTD_REF);
  int qtdP = EEPROM.read(EEPROM_ADDR_QTD_POR);
  if (qtdR < 1 || qtdR > MAX_REFEICOES) return false;                     // Trava de seguranca contra memoria corrompida
  if (qtdP < 1 || qtdP > MAX_PORCOES) return false;

  int h[MAX_REFEICOES], m[MAX_REFEICOES];                                 // Vetores temporários (locais)
  for (int i = 0; i < qtdR; i++) {
    h[i] = EEPROM.read(EEPROM_ADDR_HORAS + i);
    m[i] = EEPROM.read(EEPROM_ADDR_MINUTOS + i);
    if (h[i] > 23 || m[i] > 59) return false;                             // Horário impossível = dado inválido
  }

  qtdRefeicoes = qtdR;                                                    // Tudo valido: copia para a RAM
  qtdPorcoes = qtdP;
  for (int i = 0; i < qtdR; i++) {
    horaRefeicao[i] = h[i];
    minutoRefeicao[i] = m[i];
  }
  Serial.println(F("[EEPROM] Configuracao recuperada."));
  return true;
}

// ---- MOTOR (servo 360) ----
// Servo 360 = servo de ROTAÇÃO CONTÍNUA. Diferente do servo comum (que vai para um ângulo), ele não
// tem posição: o pulso enviado controla VELOCIDADE e SENTIDO.
//   pulso = 1500 us -> parado (o valor exato varia de servo para servo, por isso é ajustável)
//   pulso > 1500    -> gira para um lado (quanto mais longe de 1500, mais rápido)
//   pulso < 1500    -> gira para o outro lado (ex.: 1300 inverte o sentido)
// Como não há sensor de posição, a quantidade de ração é controlada pelo TEMPO de giro.
const int PULSO_PARAR = 1500;             // Microssegundos: ajustar ate o servo ficar imovel
const int PULSO_GIRAR = 1700;             // Microssegundos: ajustar sentido e velocidade
Servo motor;                              // Servo 360 graus (rotacao continua)
const byte PINO_SERVO = 9;                // Sinal do servo no pino 9
const unsigned long TEMPO_POR_PORCAO = 2000; // Milissegundos de giro por porcao (MEDIR NA PRATICA com a racao)
bool funcionando = false;                 // true enquanto o motor esta girando
bool comandoAnterior = false;             // Para detectar a mudanca false -> true
unsigned long inicio = 0;                 // Hora (millis) em que o motor comecou a girar
unsigned long duracao = 0;                // Quanto tempo ele deve girar neste despejo
bool comandoMotor = false;                // "Pedido" de despejo vindo da Fase 4 (vale por 1 ciclo do loop)
uint8_t porcoesMotor = 0;                 // Porcoes do pedido atual

// ---- MOTOR: giro por tempo, sem delay() ----
// comando: false -> true inicia o despejo | porcoes: quantas porcoes (tempo = TEMPO_POR_PORCAO x porcoes)
// Esta função é chamada em TODA volta do loop(). Ela faz duas coisas:
//   1) se chegou um pedido novo, liga o motor e anota a hora de início;
//   2) se o motor está ligado e o tempo acabou, desliga.
void controlarMotor(bool comando, uint8_t porcoes) {
  unsigned long agora = millis();

  // "Detecção de borda": só nos interessa a MUDANÇA de false para true, não o fato de estar true.
  // Assim, um pedido que fique true por vários ciclos não reinicia o motor várias vezes.
  bool novoComando = comando && !comandoAnterior;   // Detecta a mudanca de false para true
  comandoAnterior = comando;

  // Só inicia se for um pedido novo, o motor estiver parado e houver pelo menos 1 porção
  if (novoComando && !funcionando && porcoes > 0) {
    duracao = TEMPO_POR_PORCAO * (unsigned long) porcoes;   // (unsigned long) evita estouro: 2000 x 5 passa de 16 bits
    inicio = agora;
    funcionando = true;
    motor.writeMicroseconds(PULSO_GIRAR);
  }

  // Subtrair os tempos (agora - inicio) funciona corretamente mesmo quando o millis() "dá a volta" (~49 dias)
  if (funcionando && agora - inicio >= duracao) {   // Encerra quando atingir o tempo calculado
    motor.writeMicroseconds(PULSO_PARAR);
    funcionando = false;
  }
}


// ==========================================
// SETUP
// ==========================================
// Roda UMA única vez, quando o Arduino liga ou é resetado. Serve para preparar tudo.
void setup() {
  Serial.begin(9600);   // Abre o Monitor Serial a 9600 baud (útil para depurar; mensagens [RTC], [EEPROM], [MOTOR])
  Wire.begin();         // Liga o barramento I2C (usado pelo LCD e pelo RTC)

  lcd.init();           // Inicializa o display
  lcd.backlight();      // Acende a luz de fundo

  // Configura os pinos dos botoes usando o resistor interno do Arduino (INPUT_PULLUP)
  // Isso significa que solto = HIGH (1) e apertado = LOW (0)
  // (Por isso o botão é ligado entre o pino e o GND, sem resistor externo.)
  pinMode(pinoBotaoMuda, INPUT_PULLUP);
  pinMode(pinoBotaoConfirma, INPUT_PULLUP);

  // Motor: liga o servo ja parado
  // (Sem isso, o servo 360 poderia sair girando sozinho ao ligar.)
  motor.attach(PINO_SERVO);
  motor.writeMicroseconds(PULSO_PARAR);

  // RTC: se falhar, a Fase 4 mostra "Aguardando RTC" e tenta de novo a cada segundo
  rtcOk = iniciarRTC();
  if (!rtcOk) {
    Serial.println(F("[ALERTA] RTC nao detectado no barramento I2C!"));
  }

  // EEPROM: se ja existe configuracao valida, pula o menu e vai direto para a contagem
  if (carregarConfigEEPROM()) {
    iniciarEspera("Config. salva OK", 4);   // Mostra 1 s e entra na Fase 4
  } else {
    atualizarTelaFase1();                   // Primeira vez: comeca pelo menu
  }
}


// ==========================================
// LOOP
// ==========================================
// Roda PARA SEMPRE, milhares de vezes por segundo. Cada volta:
//   1) lê os botões;
//   2) executa a função da fase atual;
//   3) cuida do motor.
// Como nenhuma função trava esperando, o ciclo é rápido e nada "perde a vez".
void loop() {
  atualizarBotoes(); // Lê os botões UMA vez por ciclo e preenche clicouMuda / clicouConfirma para todas as fases

  // Máquina de Estados: O Arduino só entra no bloco que corresponde à fase atual
  if (faseAtual == 1) {
    executarFase1();
  }
  else if (faseAtual == 98) {
    executarConfirmacao(); // Estado Coringa de Confirma? (serve para qualquer fase)
  }
  else if (faseAtual == 99) {
    executarEspera(); // Fase "Coringa" que trava a tela sem travar o Arduino
  }
  else if (faseAtual == 2) {
    executarFase2();
  }
  else if (faseAtual == 3) {
    executarFase3();
  }
  else if (faseAtual == 4) {
    executarFase4(); // Fase 4 (contagem regressiva + despejo), fica aqui para sempre
  }

  // O motor precisa ser chamado em TODO ciclo do loop (e sem delay() em lugar nenhum)
  controlarMotor(comandoMotor, porcoesMotor);
  // ORDEM IMPORTA: executarFase4() pode ter ligado comandoMotor = true neste mesmo ciclo.
  // Logo acima, controlarMotor() vê esse "true" e liga o motor; em seguida zeramos o pedido.
  comandoMotor = false;   // O pedido vale so 1 ciclo: controlarMotor detecta a borda false -> true
}


// ==========================================
// ========= LEITURA DOS BOTÕES =============
// ==========================================
// Cada fase só pergunta "clicouMuda?" / "clicouConfirma?".
// O estado só é aceito depois que a leitura fica estável por 'tempoDebounce' ms.
// Retorna true UMA vez, no ciclo em que o botão é apertado de forma estável.
//
// Os parâmetros com '&' são "referências": a função altera as variáveis ORIGINAIS de quem chamou,
// em vez de trabalhar numa cópia. É assim que a MESMA função serve para os dois botões,
// cada um com as suas próprias variáveis (estavel, leituraAnt, tMudanca).
bool lerClique(int pino, bool &estavel, bool &leituraAnt, unsigned long &tMudanca) {
  bool leitura = digitalRead(pino);   // Lê o estado físico atual do pino
  bool clique = false;                // Por padrão, nenhum clique neste ciclo

  if (leitura != leituraAnt) {        // O pino mudou desde a última leitura (pode ser ruído)
    tMudanca = millis();              // Reinicia o cronômetro de estabilidade
    leituraAnt = leitura;             // Guarda a nova leitura bruta
  }

  // Só aceita a mudança se a leitura ficou estável por mais que o tempo de debounce
  if ((millis() - tMudanca) > tempoDebounce && leitura != estavel) {
    estavel = leitura;                // Atualiza o estado confirmado
    if (estavel == LOW) {             // Estável em LOW = apertou de verdade (soltar não gera clique)
      clique = true;
    }
  }
  return clique;
}

// Atualiza os dois "avisos de clique" de uma vez. Chamada no início de cada volta do loop().
void atualizarBotoes() {
  clicouMuda = lerClique(pinoBotaoMuda, estavelMuda, leituraAntMuda, tMudancaMuda);
  clicouConfirma = lerClique(pinoBotaoConfirma, estavelConfirma, leituraAntConfirma, tMudancaConfirma);
}


// ==========================================
// ========= FUNÇÕES DA FASE 1 ==============
// ==========================================
// Fluxo: "Muda" escolhe a quantidade (1 a MAX_REFEICOES, volta ao 1 depois do máximo)
//        "Confirma" abre a confirmação (Coringa 98) -> Sim: segue para a Fase 2 | Nao: volta a escolher
void executarFase1() {
  // LÓGICA DO BOTÃO "MUDA" (Incrementa refeições)
  if (clicouMuda) {
    qtdRefeicoes++; // Adiciona 1 na conta

    // Lógica de "capagem": se passar de 5, volta para 1
    if (qtdRefeicoes > MAX_REFEICOES) {
      qtdRefeicoes = 1;
    }

    // Apaga o display antigo e escreve o número novo
    // (Só redesenhamos quando houve clique; redesenhar a todo ciclo faria o LCD piscar.)
    atualizarTelaFase1();
  }

  // LÓGICA DO BOTÃO "CONFIRMA?" (Pede confirmação)
  if (clicouConfirma) {
    char texto[17];                                                   // Buffer local para montar o texto da tela
    // snprintf monta um texto "formatado": %d é substituído pelo número. O 'sizeof(texto)' impede
    // de escrever além do tamanho do buffer (16 caracteres da linha do LCD + 1 do terminador '\0').
    snprintf(texto, sizeof(texto), "Confirma %d ref?", qtdRefeicoes); // Monta algo como "Confirma 3 ref?"
    pedirConfirmacao(texto, concluirFase1, voltarFase1);              // Delega para o Coringa 98 (Sim -> concluirFase1 / Nao -> voltarFase1)
    // Repare: passamos os NOMES das funções, sem parênteses. Estamos entregando "qual função chamar", não chamando agora.
  }
}

// Executada pelo Coringa 98 quando o usuário responde "Sim" na Fase 1
void concluirFase1() {
  char texto[17];
  snprintf(texto, sizeof(texto), "Salvo: %d", qtdRefeicoes);
  iniciarEspera(texto, 2);                              // Mostra a mensagem por 1s e segue para a Fase 2
}

// Executada pelo Coringa 98 quando o usuário responde "Nao" na Fase 1 (errou)
void voltarFase1() {
  faseAtual = 1;                                        // Sai da confirmação (98) e volta para a Fase 1
  atualizarTelaFase1();
}

// Função que desenha a interface estática da Fase 1
void atualizarTelaFase1() {
  lcd.clear();
  lcd.setCursor(0, 0);          // setCursor(coluna, linha): (0,0) = canto superior esquerdo
  lcd.print("Qtd Refeicoes:"); // Usa caracteres sem acento para o LCD não imprimir símbolos bizarros

  lcd.setCursor(0, 1);          // Linha de baixo
  lcd.print(qtdRefeicoes);
}


// ==========================================
// ========= FUNÇÕES DA FASE 2 ==============
// ==========================================
// Fluxo: ajusta Hora --(Confirma)--> ajusta Minuto --(Confirma)--> Coringa 98 "Confirma HH:MM?"
//   Nao                 -> volta a ajustar a MESMA refeição
//   Sim e faltam mais   -> ajusta a PRÓXIMA refeição
//   Sim e era a última  -> segue para a Fase 3
//
// Idéia do "rascunho": o usuário mexe em horaTemp/minutoTemp. Só quando responde "Sim" o valor
// é copiado para os vetores definitivos (horaRefeicao[] / minutoRefeicao[]).

// Chamada uma única vez quando a Fase 2 começa (reinicia o estado e prepara a tela)
void iniciarFase2() {
  indiceRefeicao = 0;
  subEtapaFase2 = ETAPA_HORA;
  horaTemp = 8;
  minutoTemp = 0;
  // >>> PONTO DE INTEGRAÇÃO RTC (opcional): se quiserem que o ajuste comece na hora atual do relógio,
  // >>> é só atribuir aqui o valor lido do RTC em horaTemp e minutoTemp, no lugar dos valores provisórios acima.
  atualizarTelaFase2();
}

void executarFase2() {
  // Botão "Muda": o que ele incrementa depende da sub-etapa
  if (clicouMuda) {
    if (subEtapaFase2 == ETAPA_HORA) {
      horaTemp++;
      if (horaTemp > 23) {
        horaTemp = 0;                        // Volta para 0h (relógio de 24 horas)
      }
    } else {
      minutoTemp += PASSO_MINUTO;            // Soma 5 (ou o valor de PASSO_MINUTO)
      if (minutoTemp > 59) {
        minutoTemp = 0;
      }
    }
    atualizarTelaFase2();
  }

  // Botão "Confirma": avança de hora para minuto, e de minuto para a confirmação
  if (clicouConfirma) {
    if (subEtapaFase2 == ETAPA_HORA) {
      subEtapaFase2 = ETAPA_MINUTO;          // Hora definida, agora ajusta os minutos
      atualizarTelaFase2();
    } else {
      char hhmm[6];                          // "HH:MM" = 5 caracteres + terminador '\0' = 6
      char texto[17];
      formatarHorario(hhmm, horaTemp, minutoTemp);
      snprintf(texto, sizeof(texto), "Confirma %s?", hhmm);   // %s insere um texto. Ex.: "Confirma 08:30?"
      pedirConfirmacao(texto, concluirHorario, reiniciarAjusteHorario);
    }
  }
}

// Executada pelo Coringa 98 quando o usuário responde "Sim" para um horário
void concluirHorario() {
  horaRefeicao[indiceRefeicao] = horaTemp;       // Rascunho vira definitivo
  minutoRefeicao[indiceRefeicao] = minutoTemp;
  // >>> PONTO DE INTEGRAÇÃO RTC: aqui o horário da refeição 'indiceRefeicao' acabou de ser confirmado
  // >>> (horaTemp / minutoTemp). É o lugar para enviar ao RTC, caso ele precise receber um a um.
  // >>> Se ele preferir ler tudo no final, os vetores horaRefeicao[] e minutoRefeicao[] já estarão completos.
  indiceRefeicao++;                              // Passa para a próxima "gaveta"

  if (indiceRefeicao < qtdRefeicoes) {
    reiniciarAjusteHorario();            // Ainda faltam refeições (sim/mais): volta a ajustar a hora da próxima
  } else {
    iniciarEspera("Horarios salvos", 3); // Era a última (sim/fim): avisa por 1s e segue para a Fase 3
  }
}

// Usada tanto no "Nao" (errou) quanto no "Sim/mais": volta para o ajuste da hora, mantendo o rascunho atual
// (Mantendo o rascunho, a próxima refeição já começa com o último horário digitado, o que facilita
//  quando os horários são parecidos.)
void reiniciarAjusteHorario() {
  subEtapaFase2 = ETAPA_HORA;
  faseAtual = 2;                         // Sai da confirmação (98) e volta para a Fase 2
  atualizarTelaFase2();
}

// Converte hora e minuto em texto "HH:MM" dentro de 'destino' (precisa de pelo menos 6 posições)
void formatarHorario(char* destino, int hora, int minuto) {
  snprintf(destino, 6, "%02d:%02d", hora, minuto); // %02d completa com zero à esquerda (5 vira "05")
}

void atualizarTelaFase2() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Refeicao ");
  lcd.print(indiceRefeicao + 1);         // +1 porque o índice começa em 0
  lcd.print("/");
  lcd.print(qtdRefeicoes);               // Ex.: "Refeicao 2/3"

  lcd.setCursor(0, 1);
  char hhmm[6];
  formatarHorario(hhmm, horaTemp, minutoTemp);
  lcd.print(hhmm);

  // A setinha mostra ao usuário qual parte está sendo ajustada agora
  if (subEtapaFase2 == ETAPA_HORA) {
    lcd.print(" <hora");
  } else {
    lcd.print(" <min");
  }
}


// ==========================================
// ========= FUNÇÕES DA FASE 3 ==============
// ==========================================
// Fluxo: ajusta Porções --(Confirma)--> Coringa 98 "Confirma X por?"
//   Nao -> volta a ajustar as porções
//   Sim -> mostra "Salvo" (Coringa 99) e segue para a Fase 4

// Chamada uma única vez quando a Fase 3 começa (prepara o valor inicial e desenha a tela)
void iniciarFase3() {
  qtdPorcoes = 1;
  atualizarTelaFase3();
}

void executarFase3() {
  // Botão "Muda": incrementa as porções (com "capagem": passou do máximo, volta para 1)
  if (clicouMuda) {
    qtdPorcoes++;
    if (qtdPorcoes > MAX_PORCOES) {
      qtdPorcoes = 1;
    }
    atualizarTelaFase3();
  }

  // Botão "Confirma": pede confirmação pelo Coringa 98
  if (clicouConfirma) {
    char texto[17];
    snprintf(texto, sizeof(texto), "Confirma %d por?", qtdPorcoes);
    pedirConfirmacao(texto, concluirFase3, voltarFase3);
  }
}

// Executada pelo Coringa 98 quando o usuário responde "Sim" na Fase 3
void concluirFase3() {
  // >>> PONTO DE INTEGRAÇÃO SERVO: aqui a quantidade de porções ('qtdPorcoes') acabou de ser confirmada.
  // >>> É o lugar para enviar ao módulo do servo, caso ele precise receber agora.
  // >>> Se ele preferir ler depois, a variável global qtdPorcoes já estará com o valor final.
  // É AQUI que a configuração inteira (refeições + horários + porções) vai para a EEPROM:
  salvarConfigEEPROM();                                 // Grava refeicoes, horarios e porcoes na EEPROM
  char texto[17];
  snprintf(texto, sizeof(texto), "Salvo: %d porc", qtdPorcoes);
  iniciarEspera(texto, 4);                              // Mostra a mensagem por 1s e segue para a Fase 4
}

// Executada pelo Coringa 98 quando o usuário responde "Nao" na Fase 3 (errou)
void voltarFase3() {
  faseAtual = 3;
  atualizarTelaFase3();
}

void atualizarTelaFase3() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Quantas Porcoes:");

  lcd.setCursor(0, 1);
  lcd.print(qtdPorcoes);
}


// ==========================================
// ========= FUNÇÕES DA FASE 4 ==============   
// ==========================================
//Fluxo (igual ao fluxograma):
//CONTAGEM: mostra "Proxima: HH:MM" e "Faltam XXhYYmin" --(deu a hora)--> DESPEJO
//DESPEJO : mostra "Bom Apetite!" até o motor terminar --> volta para CONTAGEM
//A Fase 4 não tem saída: o comedouro fica nesse loop para sempre
//(a única exceção é segurar "Muda" por 3 s durante a CONTAGEM, que reabre a configuração).
//Os botões não são usados aqui, exceto por essa segurada longa.
//O RTC e o motor NÃO são tratados aqui. Esta fase só chama 3 funções (ganchos) implementadas no final do arquivo:
//lerHoraRTC(hora, minuto)  e  iniciarDespejo(porcoes)  e  despejoConcluido()   (veja o final do arquivo)
//
//TRUQUE DOS MINUTOS: para comparar horários fica mais fácil converter tudo em "minutos desde 00:00".
//Ex.: 08:30 = 8*60 + 30 = 510. O dia tem 1440 minutos (0 a 1439).

// Chamada uma única vez quando a Fase 4 começa
void iniciarFase4() {
  estadoFase4 = ESTADO4_CONTAGEM;                      // Começa contando
  ultimoDisparoMin = -1;                               // Nenhum despejo feito ainda
  proxIndice = -1;                                     // Força o primeiro desenho da tela
  proxFaltamMin = -1;
  // Subtrair o intervalo faz o "tempo desde a última leitura" já valer 1000 ms, então a primeira leitura acontece logo
  tUltimaLeituraRTC = millis() - INTERVALO_LEITURA_RTC; // Força a primeira leitura do RTC já no próximo ciclo
}

// Roda a cada ciclo do loop enquanto faseAtual == 4
void executarFase4() {
  // Segurar "Muda" por 3 s durante a contagem volta para a configuracao
  // estavelMuda == LOW  -> o botão está apertado agora;
  // millis() - tMudancaMuda -> há quanto tempo ele está nesse estado (tMudancaMuda marca quando foi apertado).
  if (estadoFase4 == ESTADO4_CONTAGEM && estavelMuda == LOW && (millis() - tMudancaMuda) > TEMPO_RECONFIGURAR) {
    iniciarEspera("Reconfigurar...", 1);               // Mostra o aviso e volta para a Fase 1
    return;                                            // Sai agora: o resto da função não deve rodar neste ciclo
  }
  if (estadoFase4 == ESTADO4_CONTAGEM) {
    executarContagem();
  } else {
    executarDespejo();
  }
}

//Calcula qual é a próxima refeição e quantos minutos faltam para ela
//'agora' = minutos desde 00:00 (0 a 1439). Se o horário de hoje já passou, conta para amanhã (+1440)
//
//Os parâmetros 'indice' e 'faltam' têm '&': a função "devolve" dois resultados escrevendo
//diretamente nas variáveis de quem chamou (uma função normal só devolve um valor).
void calcularProximaRefeicao(int agora, int &indice, int &faltam) {
  indice = 0;
  faltam = 1441;                                       // Maior que qualquer valor possível (0 a 1440): garante que a 1ª refeição vira a "menor até agora"

  for (int i = 0; i < qtdRefeicoes; i++) {
    int alvo = horaRefeicao[i] * 60 + minutoRefeicao[i];  // Horário da refeição em minutos
    int diferenca = alvo - agora;
    if (diferenca < 0) {
      diferenca += 1440;                               // Já passou hoje, então será amanhã
    }
    if (diferenca == 0 && agora == ultimoDisparoMin) {
      diferenca = 1440;                                // Esta refeição acabou de ser servida neste minuto: a próxima é só amanhã
    }
    if (diferenca < faltam) {                          // Guarda a refeição mais próxima encontrada até agora
      faltam = diferenca;
      indice = i;
    }
  }
}

//Estado CONTAGEM: consulta o RTC, atualiza a tela e dispara o despejo quando der a hora
void executarContagem() {
  // Só consulta o RTC 1 vez por segundo (não precisa ser mais rápido, pois os horários são em minutos)
  if ((millis() - tUltimaLeituraRTC) < INTERVALO_LEITURA_RTC) {
    return;                                            // Ainda não é hora de consultar o RTC de novo
  }
  tUltimaLeituraRTC = millis();

  int hora, minuto;
  if (!lerHoraRTC(hora, minuto)) {                     // Gancho do RTC. Se falhar (ou ainda não existir), avisa na tela
    if (proxIndice != -2) {                            // Só desenha se ainda não estiver desenhado (evita piscar)
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Aguardando RTC");
      proxIndice = -2;                                 // Marca que esta tela já foi desenhada (não pisca)
    }
    return;
  }

  int agora = hora * 60 + minuto;                      // Hora atual em minutos desde 00:00

  if (agora != ultimoDisparoMin) {
    ultimoDisparoMin = -1;                             // O minuto do último despejo já passou, libera para o dia seguinte
  }

  int indice, faltam;
  calcularProximaRefeicao(agora, indice, faltam);

  if (faltam == 0) {                                   // Deu a hora!
    iniciarDespejoFase4(agora);
    return;
  }

  // Só redesenha se algo mudou (evita lcd.clear() toda hora e o display piscar)
  if (indice != proxIndice || faltam != proxFaltamMin) {
    proxIndice = indice;
    proxFaltamMin = faltam;
    atualizarTelaFase4();
  }
}

// Desenha a tela da contagem regressiva
void atualizarTelaFase4() {
  char hhmm[6];
  char linha[17];
  formatarHorario(hhmm, horaRefeicao[proxIndice], minutoRefeicao[proxIndice]);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Proxima: ");
  lcd.print(hhmm);                                     // Ex.: "Proxima: 12:00"

  lcd.setCursor(0, 1);
  // Divisão inteira por 60 = horas; resto da divisão (%) por 60 = minutos. Ex.: 205 min -> 3h e 25min
  snprintf(linha, sizeof(linha), "Faltam %02dh%02dmin", proxFaltamMin / 60, proxFaltamMin % 60); // Ex.: "Faltam 03h25min"
  lcd.print(linha);
}

// Dá a partida no despejo: mostra "Bom Apetite!" e avisa o módulo do motor
void iniciarDespejoFase4(int agora) {
  ultimoDisparoMin = agora;                            // Marca este minuto como já servido (evita servir de novo no mesmo minuto)
  estadoFase4 = ESTADO4_DESPEJO;
  tInicioDespejo = millis();
  atualizarTelaBomApetite();
  iniciarDespejo(qtdPorcoes);                          // Gancho do motor: despeja 'qtdPorcoes' porções
}

// Estado DESPEJO: espera o tempo mínimo da tela e o motor terminar, depois volta à contagem
void executarDespejo() {
  // As DUAS condições precisam ser verdadeiras (&&): passou o tempo mínimo da mensagem E o motor já parou
  if ((millis() - tInicioDespejo) > TEMPO_MIN_BOM_APETITE && despejoConcluido()) {
    estadoFase4 = ESTADO4_CONTAGEM;
    proxIndice = -1;                                   // Força redesenhar a contagem
    proxFaltamMin = -1;
    tUltimaLeituraRTC = millis() - INTERVALO_LEITURA_RTC; // Força leitura imediata do RTC
  }
}

// Desenha a tela "Bom Apetite!"
void atualizarTelaBomApetite() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Bom Apetite!");
  lcd.setCursor(0, 1);
  lcd.print("Servindo ");
  lcd.print(qtdPorcoes);
  lcd.print(" porc");
}


// ==========================================
// ========= FUNÇÃO CORINGA (ESTADO 98) =====
// ==========================================
// Tela de confirmação reutilizável por qualquer fase (Fase 1, 2, 3...).
//
// COMO FUNCIONA (passo a passo):
//   1) A fase chama pedirConfirmacao(texto, funcaoSim, funcaoNao): guarda o texto e as duas funções
//      e muda faseAtual para 98.
//   2) A cada volta do loop, executarConfirmacao() roda: "Muda" alterna Sim/Nao, "Confirma" executa a função escolhida.
//   3) A função escolhida (acaoSim ou acaoNao) é quem muda faseAtual de novo, devolvendo o controle a uma fase.

// A fase chama isto para abrir a confirmação. Informa o texto e o que fazer em cada resposta.
void pedirConfirmacao(const char* texto, AcaoConfirma sim, AcaoConfirma nao) {
  strncpy(textoConfirma, texto, 16);   // Copia o texto (máx. 16 caracteres) para a variável global
  textoConfirma[16] = '\0';            // Garante o terminador de texto
  acaoSim = sim;
  acaoNao = nao;
  respostaSim = true;                  // A opção "Sim" sempre começa marcada
  faseAtual = 98;
  atualizarTelaConfirmacao();
}

// Roda a cada ciclo enquanto faseAtual == 98 (não trava o processador)
void executarConfirmacao() {
  if (clicouMuda) {                    // "Muda" alterna entre Sim e Nao
    respostaSim = !respostaSim;        // O '!' inverte: true vira false e vice-versa
    atualizarTelaConfirmacao();
  }

  if (clicouConfirma) {                // "Confirma" aceita a opção marcada
    if (respostaSim) {
      if (acaoSim != NULL) acaoSim();  // Chama a função guardada (os parênteses = executar). O teste != NULL evita chamar "nada"
    } else {
      if (acaoNao != NULL) acaoNao();
    }
  }
}

// Desenha a tela de confirmação: texto da fase em cima e opções embaixo
void atualizarTelaConfirmacao() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(textoConfirma);
  lcd.setCursor(0, 1);
  // Os colchetes [ ] marcam a opção selecionada
  if (respostaSim) {
    lcd.print("[Sim]   Nao");
  } else {
    lcd.print(" Sim   [Nao]");
  }
}


// ==========================================
// ========= FUNÇÃO CORINGA (ESTADO 99) =====
// ==========================================
// Tela de mensagem temporária reutilizável: mostra um texto por 1 segundo e depois muda para outra fase.
// Faz o papel de um delay(1000), mas SEM congelar o Arduino (usa millis()).

// Mostra uma mensagem por 1s e depois vai para a fase 'destino'
void iniciarEspera(const char* texto, int destino) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(texto);
  tempoMensagem = millis();       // Salva o exato milissegundo que a mensagem apareceu
  proximaFase = destino;          // Lembra para onde ir quando o tempo acabar
  faseAtual = 99;                 // A partir do próximo ciclo, o loop() chama executarEspera()
}

void executarEspera() {
  // O loop fica rodando isso aqui sem travar o processador.
  // Quando a diferença entre o tempo atual e o tempo da mensagem passar de 1000ms (1 segundo):
  if ((millis() - tempoMensagem) > 1000) {

    // Altera a fase do sistema para o destino que foi agendado
    faseAtual = proximaFase;

    // Já inicia a próxima fase para o LCD não ficar em branco
    // >>> Ao criar uma fase nova, lembre de adicionar a chamada dela aqui.
    if (faseAtual == 1) {
      atualizarTelaFase1();   // Volta a configuracao (reconfigurar)
    } else if (faseAtual == 2) {
      iniciarFase2();
    } else if (faseAtual == 3) {
      iniciarFase3();
    } else if (faseAtual == 4) {
      iniciarFase4();  // Inicia a Fase 4 (contagem regressiva)
    }
  }
}


// ==========================================
// ========= INTEGRACAO: RTC E MOTOR ========
// ==========================================
// São as funções "ponte" usadas pela Fase 4. Elas escondem os detalhes do hardware:
// a Fase 4 só pede "me dá a hora" ou "despeje X porções" e não precisa saber como isso é feito.
// Vantagem: para trocar o RTC ou o motor por outro modelo, basta mexer aqui e na secção 9.

// Preenche hora (0-23) e minuto (0-59) com o horario do RTC. Retorna false se o RTC nao responder
// Os '&' fazem a função escrever direto nas variáveis 'hora' e 'minuto' de quem chamou (executarContagem).
bool lerHoraRTC(int &hora, int &minuto) {
  if (!rtcOk) {
    rtcOk = iniciarRTC();                 // Tenta de novo (a Fase 4 chama 1x por segundo)
    if (!rtcOk) return false;             // Continua sem responder: avisa quem chamou
  }
  DateTime agora = rtc.now();             // Lê a data e hora completas do módulo
  hora = agora.hour();
  minuto = agora.minute();
  return true;
}

// ---- GANCHOS DO LCD (Fase 4) ----
// Chamada UMA vez quando da a hora. So registra o pedido; quem gira o motor e controlarMotor() no loop()
void iniciarDespejo(int porcoes) {
  porcoesMotor = (uint8_t) porcoes;       // (uint8_t) = converte para número de 1 byte (0 a 255)
  comandoMotor = true;                    // Levanta a "bandeira": controlarMotor() vai perceber no fim deste ciclo
  Serial.print(F("[MOTOR] Despejando "));
  Serial.print(porcoes);
  Serial.println(F(" porcao(oes)"));
}

// true quando o motor terminou e nao ha pedido pendente
bool despejoConcluido() {
  return !funcionando && !comandoMotor;
}
