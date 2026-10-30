// ==========================================
// BIBLIOTECAS E CONFIGURAÇÃO DO DISPLAY
// ==========================================
#include <Wire.h>               // Biblioteca padrão para comunicação I2C
#include <LiquidCrystal_I2C.h>  // Biblioteca para controlar o LCD via I2C

// Cria o objeto 'lcd' no endereço 0x27, com 16 colunas e 2 linhas
LiquidCrystal_I2C lcd(0x27, 16, 2);


// ==========================================
// TIPOS PERSONALIZADOS   <-- NOVO
// ==========================================
// NOVO: Fica no topo para a Arduino IDE enxergar o tipo antes de gerar os protótipos das funções
// NOVO: Tipo "ponteiro para função sem parâmetros" (usado pela confirmação coringa para saber o que fazer em Sim/Nao)
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
const int pinoBotaoMuda = 2;     // Botão que incrementa o valor da tela atual   (ALTERADO: texto genérico, serve para todas as fases)
const int pinoBotaoConfirma = 3; // Botão que confirma / avança                   (ALTERADO: texto genérico, serve para todas as fases)


// ==========================================
// 3. VARIÁVEIS PARA CONTROLE DE CLIQUE (DEBOUNCE)
// ==========================================
// ALTERADO: o debounce agora é por ESTABILIDADE do sinal (trata ruído ao apertar E ao soltar, necessário em botões reais)
const unsigned long tempoDebounce = 50;  // Tempo (ms) que a leitura precisa ficar estável. Se ainda houver clique fantasma no botão real, suba para 80 ou 100

// ALTERADO: para cada botão, guardamos o estado estável, a última leitura bruta e a hora da última mudança do pino
bool estavelMuda = HIGH;                 // Estado confirmado (já filtrado) do botão Muda
bool leituraAntMuda = HIGH;              // Última leitura bruta do pino do botão Muda
unsigned long tMudancaMuda = 0;          // Hora em que o pino do botão Muda mudou pela última vez

bool estavelConfirma = HIGH;             // Estado confirmado (já filtrado) do botão Confirma
bool leituraAntConfirma = HIGH;          // Última leitura bruta do pino do botão Confirma
unsigned long tMudancaConfirma = 0;      // Hora em que o pino do botão Confirma mudou pela última vez

// NOVO: "Avisos" de clique válido. Valem só durante UM ciclo do loop e qualquer fase pode consultá-los
bool clicouMuda = false;      // NOVO: true no ciclo em que o botão Muda teve um clique válido
bool clicouConfirma = false;  // NOVO: true no ciclo em que o botão Confirma teve um clique válido


// ==========================================
// 4. VARIÁVEIS DO ESTADO DE ESPERA (CORINGA 99)
// ==========================================
unsigned long tempoMensagem = 0; // Guarda a hora exata em que a tela de "Salvo" apareceu
int proximaFase = 0;             // Diz para o estado Coringa (99) qual é a próxima parada


// ==========================================
// 5. VARIÁVEIS DO ESTADO DE CONFIRMAÇÃO (CORINGA 98)   <-- NOVO (seção inteira)
// ==========================================
char textoConfirma[17];             // NOVO: Texto da linha de cima da tela Confirma? (16 caracteres + terminador '\0')
AcaoConfirma acaoSim = NULL;        // NOVO: Função que será chamada se o usuário confirmar com "Sim"
AcaoConfirma acaoNao = NULL;        // NOVO: Função que será chamada se o usuário marcar "Nao"
bool respostaSim = true;            // NOVO: Opção marcada na tela (true = Sim, false = Nao)


// ==========================================
// 6. VARIÁVEIS DA FASE 2 (HORÁRIOS)   <-- NOVO (seção inteira)
// ==========================================
// ALTERADO (apenas comentário): DADOS ENTREGUES PELA CONFIGURAÇÃO AOS OUTROS MÓDULOS (RTC, servo):
//   qtdRefeicoes      -> quantas refeições por dia (1 a MAX_REFEICOES)
//   horaRefeicao[i]   -> hora (0 a 23) da refeição i
//   minutoRefeicao[i] -> minuto (0 a 59) da refeição i
//   (i vai de 0 até qtdRefeicoes - 1; só são válidos depois que a Fase 2 termina)
const int PASSO_MINUTO = 5;         // NOVO: Quanto os minutos aumentam a cada clique (mude para 1 se quiser precisão total)
const int ETAPA_HORA = 0;           // NOVO: Sub-etapa em que o usuário ajusta a hora
const int ETAPA_MINUTO = 1;         // NOVO: Sub-etapa em que o usuário ajusta os minutos

int horaRefeicao[MAX_REFEICOES];    // NOVO: Hora (0 a 23) já confirmada de cada refeição (uma posição por refeição)
int minutoRefeicao[MAX_REFEICOES];  // NOVO: Minuto (0 a 59) já confirmado de cada refeição
int indiceRefeicao = 0;             // NOVO: Qual refeição está sendo configurada (0 = primeira, 1 = segunda...)
int horaTemp = 8;                   // NOVO: Hora "rascunho", só é gravada no vetor se o usuário confirmar
int minutoTemp = 0;                 // NOVO: Minuto "rascunho", mesma lógica da hora
int subEtapaFase2 = ETAPA_HORA;     // NOVO: Sub-etapa atual dentro da Fase 2


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
  atualizarBotoes(); // NOVO: Lê os botões UMA vez por ciclo e preenche clicouMuda / clicouConfirma para todas as fases

  // Máquina de Estados: O Arduino só entra no bloco que corresponde à fase atual
  if (faseAtual == 1) {
    executarFase1();
  }
  else if (faseAtual == 98) {
    executarConfirmacao(); // NOVO: Estado Coringa de Confirma? (serve para qualquer fase)
  }
  else if (faseAtual == 99) {
    executarEspera(); // Fase "Coringa" que trava a tela sem travar o Arduino
  }
  else if (faseAtual == 2) {
    executarFase2(); // ALTERADO: antes estava comentada ("será implementado amanhã"); agora chama a Fase 2
  }
}


// ==========================================
// ========= LEITURA DOS BOTÕES =============   <-- NOVO (bloco inteiro)
// ==========================================
// NOVO: Antes, cada fase lia os botões e fazia o debounce por conta própria (código repetido).
// NOVO: Agora isso é feito aqui, uma vez só. As fases apenas perguntam "clicouMuda?" / "clicouConfirma?".

// ALTERADO: debounce por estabilidade. O estado só é aceito depois que a leitura fica estável por 'tempoDebounce' ms.
// Qualquer oscilação (ao apertar ou ao soltar) reinicia a contagem, então o ruído da mola é ignorado.
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

// ALTERADO: agora só chama lerClique() para cada botão (a leitura continua acontecendo também durante as esperas)
void atualizarBotoes() {
  clicouMuda = lerClique(pinoBotaoMuda, estavelMuda, leituraAntMuda, tMudancaMuda);
  clicouConfirma = lerClique(pinoBotaoConfirma, estavelConfirma, leituraAntConfirma, tMudancaConfirma);
}


// ==========================================
// ========= FUNÇÕES DA FASE 1 ==============
// ==========================================

// ALTERADO: o corpo inteiro foi simplificado, sem leitura de botão nem debounce (agora vêm de atualizarBotoes)
void executarFase1() {
  // ----------------------------------------------------
  // LÓGICA DO BOTÃO "MUDA" (Incrementa refeições)
  // ----------------------------------------------------
  if (clicouMuda) {      // ALTERADO: antes era o bloco gigante de estado/debounce; agora basta consultar o aviso
    qtdRefeicoes++; // Adiciona 1 na conta
    
    // Lógica de "capagem": se passar de 5, zera e volta para 1
    if (qtdRefeicoes > MAX_REFEICOES) {
      qtdRefeicoes = 1; 
    }
    
    // Apaga o display antigo e escreve o número novo
    atualizarTelaFase1(); 
  }

  // ----------------------------------------------------
  // LÓGICA DO BOTÃO "CONFIRMA?" (Pede confirmação)
  // ----------------------------------------------------
  if (clicouConfirma) {  // ALTERADO: antes salvava e avançava direto; agora pede confirmação (como no fluxograma)
    char texto[17];                                                  // NOVO: Buffer local para montar o texto da tela
    snprintf(texto, sizeof(texto), "Confirma %d ref?", qtdRefeicoes); // NOVO: Monta algo como "Confirma 3 ref?"
    pedirConfirmacao(texto, concluirFase1, voltarFase1);             // NOVO: Delega para o Coringa 98 (Sim -> concluirFase1 / Nao -> voltarFase1)
  }
} 

// NOVO: Executada pelo Coringa 98 quando o usuário responde "Sim" na Fase 1
void concluirFase1() {
  char texto[17];                                       // NOVO: Buffer local para a mensagem
  snprintf(texto, sizeof(texto), "Salvo: %d", qtdRefeicoes); // NOVO: Monta "Salvo: 3" (mesma mensagem que já existia)
  iniciarEspera(texto, 2);                              // NOVO: Mostra a mensagem por 1s e segue para a Fase 2
}

// NOVO: Executada pelo Coringa 98 quando o usuário responde "Nao" na Fase 1 (errou)
void voltarFase1() {
  faseAtual = 1;          // NOVO: Devolve o controle para a Fase 1
  atualizarTelaFase1();   // NOVO: Redesenha a tela de quantidade de refeições
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
// ========= FUNÇÕES DA FASE 2 ==============   <-- NOVO (bloco inteiro)
// ==========================================
// NOVO: Fluxo: ajusta Hora --(Confirma)--> ajusta Minuto --(Confirma)--> Coringa 98 "Confirma HH:MM?"
// NOVO:   Nao                 -> volta a ajustar a MESMA refeição
// NOVO:   Sim e faltam mais   -> ajusta a PRÓXIMA refeição
// NOVO:   Sim e era a última  -> segue para a Fase 3

// NOVO: Chamada uma única vez quando a Fase 2 começa (reinicia o estado e prepara a tela)
void iniciarFase2() {
  indiceRefeicao = 0;                  // NOVO: Começa pela primeira refeição
  subEtapaFase2 = ETAPA_HORA;          // NOVO: Começa ajustando a hora
  horaTemp = 8;                        // NOVO: Valor inicial provisório da hora (08)
  minutoTemp = 0;                      // NOVO: Valor inicial provisório do minuto (00)
  // >>> PONTO DE INTEGRAÇÃO RTC (opcional): se quiserem que o ajuste comece na hora atual do relógio,
  // >>> é só atribuir aqui o valor lido do RTC em horaTemp e minutoTemp, no lugar dos valores provisórios acima.
  atualizarTelaFase2();                // NOVO: Desenha a primeira tela da Fase 2
}

void executarFase2() {
  // NOVO: Botão "Muda": o que ele incrementa depende da sub-etapa
  if (clicouMuda) {
    if (subEtapaFase2 == ETAPA_HORA) {
      horaTemp++;                            // NOVO: Soma 1 hora
      if (horaTemp > 23) {                   // NOVO: Passou de 23h?
        horaTemp = 0;                        // NOVO: Volta para 0h (relógio de 24 horas)
      }
    } else {
      minutoTemp += PASSO_MINUTO;            // NOVO: Soma o passo definido (5 em 5)
      if (minutoTemp > 59) {                 // NOVO: Passou de 59?
        minutoTemp = 0;                      // NOVO: Volta para 0
      }
    }
    atualizarTelaFase2();                    // NOVO: Redesenha com o valor novo
  }

  // NOVO: Botão "Confirma": avança de hora para minuto, e de minuto para a confirmação
  if (clicouConfirma) {
    if (subEtapaFase2 == ETAPA_HORA) {
      subEtapaFase2 = ETAPA_MINUTO;          // NOVO: Hora definida, agora ajusta os minutos
      atualizarTelaFase2();                  // NOVO: Redesenha mostrando "<min"
    } else {
      char hhmm[6];                                        // NOVO: Buffer para "HH:MM"
      char texto[17];                                      // NOVO: Buffer para a linha de cima da confirmação
      formatarHorario(hhmm, horaTemp, minutoTemp);         // NOVO: Converte o horário rascunho em texto
      snprintf(texto, sizeof(texto), "Confirma %s?", hhmm); // NOVO: Monta "Confirma 08:00?"
      pedirConfirmacao(texto, concluirHorario, reiniciarAjusteHorario); // NOVO: Delega para o Coringa 98 (Sim -> concluirHorario / Nao -> reiniciarAjusteHorario)
    }
  }
}

// NOVO: Executada pelo Coringa 98 quando o usuário responde "Sim" para um horário
void concluirHorario() {
  horaRefeicao[indiceRefeicao] = horaTemp;                  // NOVO: Grava a hora rascunho no vetor definitivo
  minutoRefeicao[indiceRefeicao] = minutoTemp;              // NOVO: Grava o minuto rascunho no vetor definitivo
  // >>> PONTO DE INTEGRAÇÃO RTC: aqui o horário da refeição 'indiceRefeicao' acabou de ser confirmado
  // >>> (horaTemp / minutoTemp). É o lugar para enviar ao RTC, caso ele precise receber um a um.
  // >>> Se ele preferir ler tudo no final, os vetores horaRefeicao[] e minutoRefeicao[] já estarão completos.
  indiceRefeicao++;                                         // NOVO: Passa a contar a próxima refeição

  if (indiceRefeicao < qtdRefeicoes) {
    reiniciarAjusteHorario();            // NOVO: Ainda faltam refeições (sim/mais): volta a ajustar a hora da próxima
  } else {
    iniciarEspera("Horarios salvos", 3); // NOVO: Era a última (sim/fim): avisa por 1s e segue para a Fase 3
  }
}

// NOVO: Usada tanto no "Nao" (errou) quanto no "Sim/mais": volta para o ajuste da hora, mantendo o rascunho atual
void reiniciarAjusteHorario() {
  subEtapaFase2 = ETAPA_HORA;   // NOVO: Recomeça pelo ajuste da hora
  faseAtual = 2;                // NOVO: Devolve o controle para a Fase 2
  atualizarTelaFase2();         // NOVO: Redesenha a tela
}

// NOVO: Converte hora e minuto em texto "HH:MM" dentro de 'destino' (precisa de pelo menos 6 posições)
void formatarHorario(char* destino, int hora, int minuto) {
  snprintf(destino, 6, "%02d:%02d", hora, minuto); // NOVO: %02d completa com zero à esquerda (5 vira "05")
}

// ALTERADO: antes era uma função temporária que só mostrava "Fase 2 em breve"; agora desenha a tela real
void atualizarTelaFase2() {
  lcd.clear();                           // Apaga o display antigo
  lcd.setCursor(0, 0);                   // NOVO: Primeira linha
  lcd.print("Refeicao ");                // NOVO: Texto fixo
  lcd.print(indiceRefeicao + 1);         // NOVO: Número da refeição (+1 porque o índice começa em 0)
  lcd.print("/");                        // NOVO: Separador
  lcd.print(qtdRefeicoes);               // NOVO: Total escolhido na Fase 1

  lcd.setCursor(0, 1);                   // NOVO: Segunda linha
  char hhmm[6];                          // NOVO: Buffer para "HH:MM"
  formatarHorario(hhmm, horaTemp, minutoTemp); // NOVO: Converte o rascunho em texto
  lcd.print(hhmm);                       // NOVO: Mostra o horário

  if (subEtapaFase2 == ETAPA_HORA) {
    lcd.print(" <hora");                 // NOVO: Seta indicando que a hora está sendo editada
  } else {
    lcd.print(" <min");                  // NOVO: Seta indicando que os minutos estão sendo editados
  }
}


// ==========================================
// ========= FUNÇÃO CORINGA (ESTADO 98) =====   <-- NOVO (bloco inteiro)
// ==========================================
// NOVO: Tela de confirmação reutilizável por qualquer fase (Fase 1, 2, 3...).

// NOVO: A fase chama isto para abrir a confirmação. Informa o texto e o que fazer em cada resposta.
void pedirConfirmacao(const char* texto, AcaoConfirma sim, AcaoConfirma nao) {
  strncpy(textoConfirma, texto, 16);   // NOVO: Copia o texto (máx. 16 caracteres) para a variável global
  textoConfirma[16] = '\0';            // NOVO: Garante o terminador de texto
  acaoSim = sim;                       // NOVO: Guarda a função do "Sim"
  acaoNao = nao;                       // NOVO: Guarda a função do "Nao"
  respostaSim = true;                  // NOVO: A opção "Sim" sempre começa marcada
  faseAtual = 98;                      // NOVO: Entra no estado Coringa de confirmação
  atualizarTelaConfirmacao();          // NOVO: Desenha a tela
}

// NOVO: Roda a cada ciclo enquanto faseAtual == 98 (não trava o processador)
void executarConfirmacao() {
  if (clicouMuda) {                    // NOVO: "Muda" alterna entre Sim e Nao
    respostaSim = !respostaSim;        // NOVO: Inverte a opção marcada
    atualizarTelaConfirmacao();        // NOVO: Redesenha com a opção nova
  }

  if (clicouConfirma) {                // NOVO: "Confirma" aceita a opção marcada
    if (respostaSim) {
      if (acaoSim != NULL) acaoSim();  // NOVO: Executa a função do Sim (se existir)
    } else {
      if (acaoNao != NULL) acaoNao();  // NOVO: Executa a função do Nao (se existir)
    }
  }
}

// NOVO: Desenha a tela de confirmação: texto da fase em cima e opções embaixo
void atualizarTelaConfirmacao() {
  lcd.clear();                         // NOVO: Apaga o display antigo
  lcd.setCursor(0, 0);                 // NOVO: Primeira linha
  lcd.print(textoConfirma);            // NOVO: Texto montado pela fase (ex.: "Confirma 08:00?")
  lcd.setCursor(0, 1);                 // NOVO: Segunda linha
  if (respostaSim) {
    lcd.print("[Sim]   Nao");          // NOVO: Colchetes indicam que "Sim" está marcado
  } else {
    lcd.print(" Sim   [Nao]");         // NOVO: Colchetes indicam que "Nao" está marcado
  }
}


// ==========================================
// ========= FUNÇÃO CORINGA (ESTADO 99) =====
// ==========================================

// NOVO: Mostra uma mensagem por 1s e depois vai para a fase 'destino' (substitui as linhas que se repetiam em cada fase)
void iniciarEspera(const char* texto, int destino) {
  lcd.clear();                    // NOVO: Limpa a tela
  lcd.setCursor(0, 0);            // NOVO: Primeira linha
  lcd.print(texto);               // NOVO: Mostra a mensagem
  tempoMensagem = millis();       // NOVO: Salva o exato milissegundo que a mensagem apareceu
  proximaFase = destino;          // NOVO: Diz qual é a fase de destino
  faseAtual = 99;                 // NOVO: Entra no estado de Espera Coringa
}

void executarEspera() {
  // O loop fica rodando isso aqui sem travar o processador.
  // Quando a diferença entre o tempo atual e o tempo da mensagem passar de 1000ms (1 segundo):
  if ((millis() - tempoMensagem) > 1000) {
    
    // Altera a fase do sistema para o destino que foi agendado
    faseAtual = proximaFase; 
    
    // Já imprime a estrutura estática da próxima fase para o LCD não ficar em branco
    if (faseAtual == 2) {
      iniciarFase2();  // ALTERADO: antes chamava atualizarTelaFase2(); agora inicia a fase (zera índice e etapa) e desenha
    } else if (faseAtual == 3) {
      atualizarTelaFase3();
    }
  }
}


// ==========================================
// ========= FUNÇÕES TEMPORÁRIAS ============
// ==========================================
// Mantida aqui vazia apenas para o código não dar erro de compilação hoje.
// ALTERADO: atualizarTelaFase2() saiu daqui, pois agora está implementada na seção da Fase 2.

void atualizarTelaFase3() {
  // Vazio
}