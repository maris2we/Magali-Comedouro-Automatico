// ==========================================
// BIBLIOTECAS E CONFIGURAÇÃO DO DISPLAY
// ==========================================
#include <Wire.h>               // Biblioteca padrão para comunicação I2C
#include <LiquidCrystal_I2C.h>  // Biblioteca para controlar o LCD via I2C

LiquidCrystal_I2C lcd(0x27, 16, 2);


// ==========================================
// 1. VARIÁVEIS GLOBAIS DE ESTADO E CONTROLE
// ==========================================
// O Arduino usa 'faseAtual' como o trilho de trem para saber qual tela rodar.
int faseAtual = 1; 

// Variável que guarda a quantidade de refeições escolhida pelo usuário
int qtdRefeicoes = 1;

// Limite máximo de refeições para a lógica de "capagem" (se passar disso, volta pra 1)
const int MAX_REFEICOES = 5;


// ==========================================
// 2. PINOS DOS BOTÕES
// ==========================================
const int pinoBotaoMuda = 2;     // Botão que incrementa o número de refeições
const int pinoBotaoConfirma = 3; // Botão que salva e avança de fase


// ==========================================
// 3. VARIÁVEIS PARA CONTROLE DE CLIQUE (DEBOUNCE)
// ==========================================
// Guardam se o botão estava apertado (LOW) ou solto (HIGH) no ciclo anterior do loop
bool ultimoEstadoMuda = HIGH;
bool ultimoEstadoConfirma = HIGH;

// Cronômetros para ignorar o "ruído elétrico" da mola do botão (Debounce)
unsigned long tempoUltimoClique = 0;         // Cronômetro do botão Muda
unsigned long tempoUltimoCliqueConfirma = 0; // Cronômetro do botão Confirma
const unsigned long tempoDebounce = 50;      // 50 milissegundos é o tempo de bloqueio do ruído


// ==========================================
// 4. VARIÁVEIS DO ESTADO DE ESPERA (CORINGA)
// ==========================================
unsigned long tempoMensagem = 0; // Guarda a hora exata em que a tela de "Salvo" apareceu
int proximaFase = 0;             // Diz para o estado Coringa (99) qual é a próxima parada


// ==========================================
// 5. VARIÁVEIS DA FASE 2 (HORÁRIOS)   <-- NOVO
// ==========================================
// NOVO: Quanto os minutos aumentam a cada clique (5 = 00, 05, 10...). Mude para 1 se quiser precisão total
const int PASSO_MINUTO = 5;

// NOVO: Constantes que nomeiam as 3 sub-etapas da Fase 2 (deixa o código mais legível que usar 0, 1 e 2)
const int ETAPA_HORA = 0;      // NOVO: Usuário está ajustando a hora
const int ETAPA_MINUTO = 1;    // NOVO: Usuário está ajustando os minutos
const int ETAPA_CONFIRMA = 2;  // NOVO: Tela "Confirma?" com opção Sim/Nao

// NOVO: Vetores que guardam os horários já confirmados (uma posição para cada refeição)
int horaRefeicao[MAX_REFEICOES];    // NOVO: Hora de cada refeição (0 a 23)
int minutoRefeicao[MAX_REFEICOES];  // NOVO: Minuto de cada refeição (0 a 59)

int indiceRefeicao = 0;              // NOVO: Qual refeição está sendo configurada (0 = primeira, 1 = segunda...)
int horaTemp = 8;                    // NOVO: Hora "rascunho", só é gravada no vetor se o usuário confirmar
int minutoTemp = 0;                  // NOVO: Minuto "rascunho", mesma lógica da hora
int subEtapaFase2 = ETAPA_HORA;      // NOVO: Sub-etapa atual dentro da Fase 2
bool respostaSim = true;             // NOVO: Opção marcada na tela Confirma? (true = Sim, false = Nao)


// ==========================================
// SETUP: RODA APENAS UMA VEZ AO LIGAR
// ==========================================
void setup() {
  lcd.init();      
  lcd.backlight(); 
  
  pinMode(pinoBotaoMuda, INPUT_PULLUP);
  pinMode(pinoBotaoConfirma, INPUT_PULLUP);
  
  // Chama a função que desenha o texto da Fase 1 no display pela primeira vez
  atualizarTelaFase1(); 
}


// ==========================================
// LOOP: RODA MILHARES DE VEZES POR SEGUNDO
// ==========================================
void loop() {
  // Máquina de Estados: O Arduino só entra no bloco que corresponde à fase atual
  if (faseAtual == 1) {
    executarFase1();
  }
  else if (faseAtual == 99) {
    executarEspera(); // Fase "Coringa" que trava a tela sem travar o Arduino
  }
  else if (faseAtual == 2) {
    executarFase2(); // Chama a Fase 2
  }
}


// ==========================================
// ========= FUNÇÕES DA FASE 1 ==============
// ==========================================

void executarFase1() {
  // Lê o estado físico atual dos botões
  bool estadoMuda = digitalRead(pinoBotaoMuda);
  bool estadoConfirma = digitalRead(pinoBotaoConfirma);

  // ----------------------------------------------------
  // LÓGICA DO BOTÃO "MUDA" (Incrementa refeições)
  // ----------------------------------------------------
  // Verifica se o botão foi apertado AGORA (LOW) e se estava SOLTO antes (HIGH)
  if (estadoMuda == LOW && ultimoEstadoMuda == HIGH) {
    
    // Verifica se já passou tempo suficiente desde o último clique para não contar ruído duplo
    if((millis() - tempoUltimoClique) > tempoDebounce){
      qtdRefeicoes++;
      
      // Lógica de "capagem": se passar de 5, zera e volta para 1
      if (qtdRefeicoes > MAX_REFEICOES) {
        qtdRefeicoes = 1; 
      }
      
      // Apaga o display antigo e escreve o número novo
      atualizarTelaFase1(); 
      
      // Registra a hora exata deste clique válido
      tempoUltimoClique = millis();
    }
   }
  // Atualiza a memória do botão para o próximo ciclo do loop
  ultimoEstadoMuda = estadoMuda;


  // ----------------------------------------------------
  // LÓGICA DO BOTÃO "CONFIRMA?" (Avança fase)
  // ----------------------------------------------------
  // Verifica transição de solto para apertado
  if (estadoConfirma == LOW && ultimoEstadoConfirma == HIGH) {
    
    // Debounce do confirmar
    if((millis() - tempoUltimoCliqueConfirma) > tempoDebounce) { 
      
      // 1. Limpa a tela e avisa o usuário que salvou
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Salvo: ");
      lcd.print(qtdRefeicoes);

      // 2. Prepara a Máquina de Estados para a transição não-bloqueante
      tempoMensagem = millis(); // Salva o exato milissegundo que a mensagem apareceu
      proximaFase = 2;          // Diz para a transição que o destino é a Fase 2
      faseAtual = 99;           // Muda o estado principal para 99 (Espera)
        
      // Registra a hora do clique
      tempoUltimoCliqueConfirma = millis();
    }
  }
  // Atualiza a memória do botão de confirmar
  ultimoEstadoConfirma = estadoConfirma;
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
// NOVO: Fluxo (usa só os 2 botões que já existem):
//   Hora  --(Confirma)--> Minuto --(Confirma)--> "Confirma HH:MM?"
//   Na tela de confirmação, "Muda" alterna entre Sim/Nao e "Confirma" aceita a opção marcada:
//     Nao                -> volta para ajustar a MESMA refeição (errou)
//     Sim e faltam mais  -> vai para a PRÓXIMA refeição (mais)
//     Sim e era a última -> salva tudo e avança para a Fase 3 (fim)

void executarFase2() {
  // NOVO: Lê o estado físico atual dos botões (mesmo padrão da Fase 1)
  bool estadoMuda = digitalRead(pinoBotaoMuda);
  bool estadoConfirma = digitalRead(pinoBotaoConfirma);

  // ----------------------------------------------------
  // NOVO: LÓGICA DO BOTÃO "MUDA" (o que ele faz depende da sub-etapa)
  // ----------------------------------------------------
  // NOVO: Detecta a transição de solto (HIGH) para apertado (LOW)
  if (estadoMuda == LOW && ultimoEstadoMuda == HIGH) {

    // NOVO: Debounce, ignora cliques duplicados causados por ruído
    if ((millis() - tempoUltimoClique) > tempoDebounce) {

      if (subEtapaFase2 == ETAPA_HORA) {
        // NOVO: Está ajustando a hora: soma 1
        horaTemp++;
        // NOVO: Passou de 23h? Volta para 0h (relógio de 24 horas)
        if (horaTemp > 23) {
          horaTemp = 0;
        }
      }
      else if (subEtapaFase2 == ETAPA_MINUTO) {
        // NOVO: Está ajustando os minutos: soma o passo definido (5 em 5)
        minutoTemp += PASSO_MINUTO;
        // NOVO: Passou de 59? Volta para 0
        if (minutoTemp > 59) {
          minutoTemp = 0;
        }
      }
      else {
        // NOVO: Está na tela Confirma?: alterna a opção marcada entre Sim e Nao
        respostaSim = !respostaSim;
      }

      atualizarTelaFase2();          // NOVO: Redesenha a tela com o valor novo
      tempoUltimoClique = millis();  // NOVO: Registra a hora deste clique válido
    }
  }
  // NOVO: Atualiza a memória do botão Muda para o próximo ciclo
  ultimoEstadoMuda = estadoMuda;


  // ----------------------------------------------------
  // NOVO: LÓGICA DO BOTÃO "CONFIRMA" (avança a sub-etapa)
  // ----------------------------------------------------
  // NOVO: Detecta a transição de solto para apertado
  if (estadoConfirma == LOW && ultimoEstadoConfirma == HIGH) {

    // NOVO: Debounce do botão confirmar
    if ((millis() - tempoUltimoCliqueConfirma) > tempoDebounce) {

      if (subEtapaFase2 == ETAPA_HORA) {
        // NOVO: Hora definida, passa para o ajuste dos minutos
        subEtapaFase2 = ETAPA_MINUTO;
      }
      else if (subEtapaFase2 == ETAPA_MINUTO) {
        // NOVO: Minuto definido, passa para a tela de confirmação
        subEtapaFase2 = ETAPA_CONFIRMA;
        respostaSim = true; // NOVO: A opção "Sim" já começa marcada
      }
      else {
        // NOVO: Está na tela Confirma?: age conforme a opção marcada
        if (respostaSim) {
          // NOVO: Usuário disse SIM: grava o rascunho nos vetores definitivos
          horaRefeicao[indiceRefeicao] = horaTemp;
          minutoRefeicao[indiceRefeicao] = minutoTemp;
          indiceRefeicao++; // NOVO: Passa a contar a próxima refeição

          if (indiceRefeicao < qtdRefeicoes) {
            // NOVO: Ainda faltam refeições (sim/mais): volta a ajustar a hora da próxima
            subEtapaFase2 = ETAPA_HORA;
          }
          else {
            // NOVO: Era a última refeição (sim/fim): avisa que salvou
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Horarios salvos");

            // NOVO: Usa o estado Coringa (99) para esperar 1s sem travar, e depois ir à Fase 3
            tempoMensagem = millis(); // NOVO: Marca quando a mensagem apareceu
            proximaFase = 3;          // NOVO: Destino depois da espera é a Fase 3
            faseAtual = 99;           // NOVO: Entra no estado de Espera Coringa
          }
        }
        else {
          // NOVO: Usuário disse NAO (errou): volta a ajustar a MESMA refeição, mantendo o rascunho
          subEtapaFase2 = ETAPA_HORA;
        }
      }

      // NOVO: Só redesenha se ainda estiver na Fase 2 (se foi para 99, preserva a mensagem "Horarios salvos")
      if (faseAtual == 2) {
        atualizarTelaFase2();
      }
      tempoUltimoCliqueConfirma = millis(); // NOVO: Registra a hora do clique
    }
  }
  // NOVO: Atualiza a memória do botão Confirma para o próximo ciclo
  ultimoEstadoConfirma = estadoConfirma;
}

// NOVO: Função auxiliar que imprime um número sempre com 2 dígitos (ex.: 5 vira "05")
void imprimirDoisDigitos(int valor) {
  if (valor < 10) {
    lcd.print("0"); // NOVO: Completa com zero à esquerda
  }
  lcd.print(valor);  // NOVO: Imprime o número em si
}

// ALTERADO: antes era uma função temporária que só mostrava "Fase 2 em breve"; agora desenha a tela real
void atualizarTelaFase2() {
  lcd.clear(); // Apaga o display antigo

  if (subEtapaFase2 == ETAPA_CONFIRMA) {
    // NOVO: ----- Tela "Confirma HH:MM?" -----
    lcd.setCursor(0, 0);         // NOVO: Primeira linha
    lcd.print("Confirma ");      // NOVO: Texto fixo
    imprimirDoisDigitos(horaTemp);   // NOVO: Hora escolhida
    lcd.print(":");                  // NOVO: Separador
    imprimirDoisDigitos(minutoTemp); // NOVO: Minuto escolhido
    lcd.print("?");                  // NOVO: Fecha a pergunta

    lcd.setCursor(0, 1);         // NOVO: Segunda linha
    if (respostaSim) {
      lcd.print("[Sim]   Nao");  // NOVO: Colchetes indicam que "Sim" está marcado
    } else {
      lcd.print(" Sim   [Nao]"); // NOVO: Colchetes indicam que "Nao" está marcado
    }
  }
  else {
    // NOVO: ----- Tela de ajuste de hora ou minuto -----
    lcd.setCursor(0, 0);                 // NOVO: Primeira linha
    lcd.print("Refeicao ");               // NOVO: Texto fixo
    lcd.print(indiceRefeicao + 1);       // NOVO: Número da refeição (+1 porque o índice começa em 0)
    lcd.print("/");                      // NOVO: Separador
    lcd.print(qtdRefeicoes);             // NOVO: Total de refeições escolhido na Fase 1

    lcd.setCursor(0, 1);                 // NOVO: Segunda linha
    imprimirDoisDigitos(horaTemp);       // NOVO: Mostra a hora
    lcd.print(":");                      // NOVO: Separador
    imprimirDoisDigitos(minutoTemp);     // NOVO: Mostra o minuto

    // NOVO: Seta indicando qual valor está sendo editado agora
    if (subEtapaFase2 == ETAPA_HORA) {
      lcd.print(" <hora");  // NOVO: Editando a hora
    } else {
      lcd.print(" <min");   // NOVO: Editando os minutos
    }
  }
}


// ==========================================
// ========= FUNÇÃO CORINGA (ESTADO 99) =====
// ==========================================
void executarEspera() {
  // O loop fica rodando isso aqui sem travar o processador.
  // Quando a diferença entre o tempo atual e o tempo da mensagem passar de 1000ms (1 segundo):
  if ((millis() - tempoMensagem) > 1000) {
    
    // Altera a fase do sistema para o destino que foi agendado
    faseAtual = proximaFase; 
    
    // Já imprime a estrutura estática da próxima fase para o LCD não ficar em branco
    if (faseAtual == 2) {
      atualizarTelaFase2();
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
