// ==========================================
// BIBLIOTECAS E CONFIGURAÇÃO DO DISPLAY
// ==========================================
#include <Wire.h>               // Biblioteca padrão para comunicação I2C
#include <LiquidCrystal_I2C.h>  // Biblioteca para controlar o LCD via I2C

// Cria o objeto 'lcd' no endereço 0x27, com 16 colunas e 2 linhas
LiquidCrystal_I2C lcd(0x27, 16, 2);


// ==========================================
// TIPOS PERSONALIZADOS
// ==========================================
// Tipo "ponteiro para função sem parâmetros" (usado pela confirmação coringa para saber o que fazer em Sim/Nao)
typedef void (*AcaoConfirma)();


// ==========================================
// 1. VARIÁVEIS GLOBAIS DE ESTADO E CONTROLE
// ==========================================
// O Arduino usa 'faseAtual' como o trilho de trem para saber qual tela rodar.
int faseAtual = 1;

// Variável que guarda a quantidade de refeições escolhida pelo usuário
int qtdRefeicoes = 1;

// Limite máximo de refeições
const int MAX_REFEICOES = 5;


// ==========================================
// 2. PINOS DOS BOTÕES
// ==========================================
const int pinoBotaoMuda = 2;     // Botão que incrementa o valor da tela atual
const int pinoBotaoConfirma = 3; // Botão que confirma / avança


// ==========================================
// 3. VARIÁVEIS PARA CONTROLE DE CLIQUE (DEBOUNCE)
// ==========================================
const unsigned long tempoDebounce = 50;  // Tempo (ms) que a leitura precisa ficar estável. Se ainda houver clique fantasma no botão real, suba para 80 ou 100

bool estavelMuda = HIGH;                 // Estado confirmado (já filtrado) do botão Muda
bool leituraAntMuda = HIGH;              // Última leitura bruta do pino do botão Muda
unsigned long tMudancaMuda = 0;          // Hora em que o pino do botão Muda mudou pela última vez

bool estavelConfirma = HIGH;             // Estado confirmado (já filtrado) do botão Confirma
bool leituraAntConfirma = HIGH;          // Última leitura bruta do pino do botão Confirma
unsigned long tMudancaConfirma = 0;      // Hora em que o pino do botão Confirma mudou pela última vez

// "Avisos" de clique válido. Valem só durante UM ciclo do loop e qualquer fase pode consultá-los
bool clicouMuda = false;      // true no ciclo em que o botão Muda teve um clique válido
bool clicouConfirma = false;  // true no ciclo em que o botão Confirma teve um clique válido


// ==========================================
// 4. VARIÁVEIS DO ESTADO DE ESPERA (CORINGA 99)
// ==========================================
unsigned long tempoMensagem = 0; // Guarda a hora exata em que a tela de "Salvo" apareceu
int proximaFase = 0;             // Diz para o estado Coringa (99) qual é a próxima parada


// ==========================================
// 5. VARIÁVEIS DO ESTADO DE CONFIRMAÇÃO (CORINGA 98)
// ==========================================
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
// 8. VARIÁVEIS DA FASE 4 (CONTAGEM E DESPEJO)   <-- NOVO (seção inteira)
// ==========================================
// NOVO: Sub-estados da Fase 4: ficar contando o tempo ou mostrar "Bom Apetite!" durante o despejo
const int ESTADO4_CONTAGEM = 0;                       // NOVO: Mostra a próxima refeição e quanto falta
const int ESTADO4_DESPEJO = 1;                        // NOVO: Mostra "Bom Apetite!" enquanto o motor despeja

const unsigned long INTERVALO_LEITURA_RTC = 1000;     // NOVO: De quanto em quanto tempo (ms) consulta o RTC
const unsigned long TEMPO_MIN_BOM_APETITE = 3000;     // NOVO: Tempo mínimo (ms) da tela "Bom Apetite!", mesmo que o motor termine antes

int estadoFase4 = ESTADO4_CONTAGEM;                   // NOVO: Sub-estado atual da Fase 4
unsigned long tUltimaLeituraRTC = 0;                  // NOVO: Hora (millis) da última consulta ao RTC
unsigned long tInicioDespejo = 0;                     // NOVO: Hora (millis) em que o despejo começou
int ultimoDisparoMin = -1;                            // NOVO: Minuto do dia (0 a 1439) do último despejo; evita despejar duas vezes no mesmo minuto
int proxIndice = -1;                                  // NOVO: Refeição mostrada na tela (-1 = nada desenhado, -2 = tela "Aguardando RTC")
int proxFaltamMin = -1;                               // NOVO: Minutos que faltam mostrados na tela (para só redesenhar quando mudar)


// ==========================================
// SETUP
// ==========================================
void setup() {
  lcd.init();
  lcd.backlight();

  // Configura os pinos dos botões usando o resistor interno do Arduino (INPUT_PULLUP)
  // Isso significa que solto = HIGH (1) e apertado = LOW (0)
  pinMode(pinoBotaoMuda, INPUT_PULLUP);
  pinMode(pinoBotaoConfirma, INPUT_PULLUP);

  // Chama a função que desenha o texto da Fase 1 no display pela primeira vez
  atualizarTelaFase1();
}


// ==========================================
// LOOP
// ==========================================
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
    executarFase4(); // NOVO: Fase 4 (contagem regressiva + despejo), fica aqui para sempre
  }
}


// ==========================================
// ========= LEITURA DOS BOTÕES =============
// ==========================================
// Cada fase só pergunta "clicouMuda?" / "clicouConfirma?".
// O estado só é aceito depois que a leitura fica estável por 'tempoDebounce' ms.
// Retorna true UMA vez, no ciclo em que o botão é apertado de forma estável.
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

void atualizarBotoes() {
  clicouMuda = lerClique(pinoBotaoMuda, estavelMuda, leituraAntMuda, tMudancaMuda);
  clicouConfirma = lerClique(pinoBotaoConfirma, estavelConfirma, leituraAntConfirma, tMudancaConfirma);
}


// ==========================================
// ========= FUNÇÕES DA FASE 1 ==============
// ==========================================
void executarFase1() {
  // LÓGICA DO BOTÃO "MUDA" (Incrementa refeições)
  if (clicouMuda) {
    qtdRefeicoes++; // Adiciona 1 na conta

    // Lógica de "capagem": se passar de 5, volta para 1
    if (qtdRefeicoes > MAX_REFEICOES) {
      qtdRefeicoes = 1;
    }

    // Apaga o display antigo e escreve o número novo
    atualizarTelaFase1();
  }

  // LÓGICA DO BOTÃO "CONFIRMA?" (Pede confirmação)
  if (clicouConfirma) {
    char texto[17];                                                   // Buffer local para montar o texto da tela
    snprintf(texto, sizeof(texto), "Confirma %d ref?", qtdRefeicoes); // Monta algo como "Confirma 3 ref?"
    pedirConfirmacao(texto, concluirFase1, voltarFase1);              // Delega para o Coringa 98 (Sim -> concluirFase1 / Nao -> voltarFase1)
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
  faseAtual = 1;
  atualizarTelaFase1();
}

// Função que desenha a interface estática da Fase 1
void atualizarTelaFase1() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Qtd Refeicoes:"); // Usa caracteres sem acento para o LCD não imprimir símbolos bizarros

  lcd.setCursor(0, 1);
  lcd.print(qtdRefeicoes);
}


// ==========================================
// ========= FUNÇÕES DA FASE 2 ==============
// ==========================================
// Fluxo: ajusta Hora --(Confirma)--> ajusta Minuto --(Confirma)--> Coringa 98 "Confirma HH:MM?"
//   Nao                 -> volta a ajustar a MESMA refeição
//   Sim e faltam mais   -> ajusta a PRÓXIMA refeição
//   Sim e era a última  -> segue para a Fase 3

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
      minutoTemp += PASSO_MINUTO;
      if (minutoTemp > 59) {
        minutoTemp = 0;
      }
    }
    atualizarTelaFase2();
  }

  // Botão "Confirma": avança de hora para minuto, e de minuto para a confirmação
  if (clicouConfirma) {
    if (subEtapaFase2 == ETAPA_HORA) {
      subEtapaFase2 = ETAPA_MINUTO;
      atualizarTelaFase2();
    } else {
      char hhmm[6];
      char texto[17];
      formatarHorario(hhmm, horaTemp, minutoTemp);
      snprintf(texto, sizeof(texto), "Confirma %s?", hhmm);
      pedirConfirmacao(texto, concluirHorario, reiniciarAjusteHorario);
    }
  }
}

// Executada pelo Coringa 98 quando o usuário responde "Sim" para um horário
void concluirHorario() {
  horaRefeicao[indiceRefeicao] = horaTemp;
  minutoRefeicao[indiceRefeicao] = minutoTemp;
  // >>> PONTO DE INTEGRAÇÃO RTC: aqui o horário da refeição 'indiceRefeicao' acabou de ser confirmado
  // >>> (horaTemp / minutoTemp). É o lugar para enviar ao RTC, caso ele precise receber um a um.
  // >>> Se ele preferir ler tudo no final, os vetores horaRefeicao[] e minutoRefeicao[] já estarão completos.
  indiceRefeicao++;

  if (indiceRefeicao < qtdRefeicoes) {
    reiniciarAjusteHorario();            // Ainda faltam refeições (sim/mais): volta a ajustar a hora da próxima
  } else {
    iniciarEspera("Horarios salvos", 3); // Era a última (sim/fim): avisa por 1s e segue para a Fase 3
  }
}

// Usada tanto no "Nao" (errou) quanto no "Sim/mais": volta para o ajuste da hora, mantendo o rascunho atual
void reiniciarAjusteHorario() {
  subEtapaFase2 = ETAPA_HORA;
  faseAtual = 2;
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
  lcd.print(qtdRefeicoes);

  lcd.setCursor(0, 1);
  char hhmm[6];
  formatarHorario(hhmm, horaTemp, minutoTemp);
  lcd.print(hhmm);

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
// ========= FUNÇÕES DA FASE 4 ==============   <-- NOVO (bloco inteiro)
// ==========================================
//Fluxo (igual ao fluxograma):
//CONTAGEM: mostra "Proxima: HH:MM" e "Faltam XXhYYmin" --(deu a hora)--> DESPEJO
//DESPEJO : mostra "Bom Apetite!" até o motor terminar --> volta para CONTAGEM
//A Fase 4 não tem saída: o comedouro fica nesse loop para sempre.
//Os botões não são usados aqui.
//O RTC e o motor NÃO são tratados aqui. Esta fase só chama 3 funções (ganchos) que os colegas implementam:
//lerHoraRTC(hora, minuto)  e  iniciarDespejo(porcoes)  e  despejoConcluido()   (veja o final do arquivo)

// NOVO: Chamada uma única vez quando a Fase 4 começa
void iniciarFase4() {
  estadoFase4 = ESTADO4_CONTAGEM;                      // NOVO: Começa contando
  ultimoDisparoMin = -1;                               // NOVO: Nenhum despejo feito ainda
  proxIndice = -1;                                     // NOVO: Força o primeiro desenho da tela
  proxFaltamMin = -1;
  tUltimaLeituraRTC = millis() - INTERVALO_LEITURA_RTC; // NOVO: Força a primeira leitura do RTC já no próximo ciclo
}

// NOVO: Roda a cada ciclo do loop enquanto faseAtual == 4
void executarFase4() {
  if (estadoFase4 == ESTADO4_CONTAGEM) {
    executarContagem();
  } else {
    executarDespejo();
  }
}

//Calcula qual é a próxima refeição e quantos minutos faltam para ela
//'agora' = minutos desde 00:00 (0 a 1439). Se o horário de hoje já passou, conta para amanhã (+1440)
void calcularProximaRefeicao(int agora, int &indice, int &faltam) {
  indice = 0;
  faltam = 1441;                                       // NOVO: Maior que qualquer valor possível (0 a 1440)

  for (int i = 0; i < qtdRefeicoes; i++) {
    int alvo = horaRefeicao[i] * 60 + minutoRefeicao[i];  // NOVO: Horário da refeição em minutos
    int diferenca = alvo - agora;
    if (diferenca < 0) {
      diferenca += 1440;                               // NOVO: Já passou hoje, então será amanhã
    }
    if (diferenca == 0 && agora == ultimoDisparoMin) {
      diferenca = 1440;                                // NOVO: Esta refeição acabou de ser servida neste minuto: a próxima é só amanhã
    }
    if (diferenca < faltam) {
      faltam = diferenca;
      indice = i;
    }
  }
}

//Estado CONTAGEM: consulta o RTC, atualiza a tela e dispara o despejo quando der a hora
void executarContagem() {
  if ((millis() - tUltimaLeituraRTC) < INTERVALO_LEITURA_RTC) {
    return;                                            // NOVO: Ainda não é hora de consultar o RTC de novo
  }
  tUltimaLeituraRTC = millis();

  int hora, minuto;
  if (!lerHoraRTC(hora, minuto)) {                     // NOVO: Gancho do RTC. Se falhar (ou ainda não existir), avisa na tela
    if (proxIndice != -2) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Aguardando RTC");
      proxIndice = -2;                                 // NOVO: Marca que esta tela já foi desenhada (não pisca)
    }
    return;
  }

  int agora = hora * 60 + minuto;

  if (agora != ultimoDisparoMin) {
    ultimoDisparoMin = -1;                             // NOVO: O minuto do último despejo já passou, libera para o dia seguinte
  }

  int indice, faltam;
  calcularProximaRefeicao(agora, indice, faltam);

  if (faltam == 0) {                                   // NOVO: Deu a hora!
    iniciarDespejoFase4(agora);
    return;
  }

  // NOVO: Só redesenha se algo mudou (evita lcd.clear() toda hora e o display piscar)
  if (indice != proxIndice || faltam != proxFaltamMin) {
    proxIndice = indice;
    proxFaltamMin = faltam;
    atualizarTelaFase4();
  }
}

// NOVO: Desenha a tela da contagem regressiva
void atualizarTelaFase4() {
  char hhmm[6];
  char linha[17];
  formatarHorario(hhmm, horaRefeicao[proxIndice], minutoRefeicao[proxIndice]);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Proxima: ");
  lcd.print(hhmm);                                     // NOVO: Ex.: "Proxima: 12:00"

  lcd.setCursor(0, 1);
  snprintf(linha, sizeof(linha), "Faltam %02dh%02dmin", proxFaltamMin / 60, proxFaltamMin % 60); // NOVO: Ex.: "Faltam 03h25min"
  lcd.print(linha);
}

// NOVO: Dá a partida no despejo: mostra "Bom Apetite!" e avisa o módulo do motor
void iniciarDespejoFase4(int agora) {
  ultimoDisparoMin = agora;                            // NOVO: Marca este minuto como já servido
  estadoFase4 = ESTADO4_DESPEJO;
  tInicioDespejo = millis();
  atualizarTelaBomApetite();
  iniciarDespejo(qtdPorcoes);                          // NOVO: Gancho do motor: despeja 'qtdPorcoes' porções
}

// NOVO: Estado DESPEJO: espera o tempo mínimo da tela e o motor terminar, depois volta à contagem
void executarDespejo() {
  if ((millis() - tInicioDespejo) > TEMPO_MIN_BOM_APETITE && despejoConcluido()) {
    estadoFase4 = ESTADO4_CONTAGEM;
    proxIndice = -1;                                   // NOVO: Força redesenhar a contagem
    proxFaltamMin = -1;
    tUltimaLeituraRTC = millis() - INTERVALO_LEITURA_RTC; // NOVO: Força leitura imediata do RTC
  }
}

// NOVO: Desenha a tela "Bom Apetite!"
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
    respostaSim = !respostaSim;
    atualizarTelaConfirmacao();
  }

  if (clicouConfirma) {                // "Confirma" aceita a opção marcada
    if (respostaSim) {
      if (acaoSim != NULL) acaoSim();
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
  if (respostaSim) {
    lcd.print("[Sim]   Nao");
  } else {
    lcd.print(" Sim   [Nao]");
  }
}


// ==========================================
// ========= FUNÇÃO CORINGA (ESTADO 99) =====
// ==========================================

// Mostra uma mensagem por 1s e depois vai para a fase 'destino'
void iniciarEspera(const char* texto, int destino) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(texto);
  tempoMensagem = millis();       // Salva o exato milissegundo que a mensagem apareceu
  proximaFase = destino;
  faseAtual = 99;
}

void executarEspera() {
  // O loop fica rodando isso aqui sem travar o processador.
  // Quando a diferença entre o tempo atual e o tempo da mensagem passar de 1000ms (1 segundo):
  if ((millis() - tempoMensagem) > 1000) {

    // Altera a fase do sistema para o destino que foi agendado
    faseAtual = proximaFase;

    // Já inicia a próxima fase para o LCD não ficar em branco
    if (faseAtual == 2) {
      iniciarFase2();
    } else if (faseAtual == 3) {
      iniciarFase3();
    } else if (faseAtual == 4) {
      iniciarFase4();  // ALTERADO: antes era um stub vazio; agora inicia a Fase 4 (contagem regressiva)
    }
  }
}


// ==========================================
// ========= MÓDULOS DE OUTROS COLEGAS (STUBS) ========
// ==========================================
//Estas 3 funções são o ÚNICO contato da Fase 4 com o RTC e o motor.
//Cada colega apaga o stub e implementa a sua função com esta mesma assinatura.
//Nada mais no código do LCD precisa mudar.

// >>> PONTO DE INTEGRAÇÃO RTC
// Preencher 'hora' (0 a 23) e 'minuto' (0 a 59) com a hora atual do RTC e retornar true.
// Se o RTC falhar, retornar false (o LCD mostra "Aguardando RTC").
bool lerHoraRTC(int &hora, int &minuto) {
  return false;  // STUB: ainda sem RTC
}

// >>> PONTO DE INTEGRAÇÃO MOTOR
// Chamada UMA vez quando der a hora. Deve iniciar o despejo de 'porcoes' porções (sem travar o loop).
void iniciarDespejo(int porcoes) {
  // STUB: ainda sem motor
}

// >>> PONTO DE INTEGRAÇÃO MOTOR
// Deve retornar true quando o motor terminar de despejar (a tela "Bom Apetite!" some e volta a contagem).
bool despejoConcluido() {
  return true;   // STUB: sem motor, considera concluído (a tela ainda fica o tempo mínimo TEMPO_MIN_BOM_APETITE)
}
