// ==========================================
// BIBLIOTECAS E CONFIGURAÇÃO DO DISPLAY
// ==========================================
#include <Wire.h>               // Biblioteca padrão para comunicação I2C
#include <LiquidCrystal_I2C.h>  // Biblioteca para controlar o LCD via I2C

// Cria o objeto 'lcd' no endereço 0x27, com 16 colunas e 2 linhas
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
// SETUP: RODA APENAS UMA VEZ AO LIGAR
// ==========================================
void setup() {
  lcd.init();      // Inicia o display
  lcd.backlight(); // Liga a luz de fundo
  
  // Configura os pinos dos botões usando o resistor interno do Arduino (INPUT_PULLUP)
  // Isso significa que solto = HIGH (1) e apertado = LOW (0)
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
    // executarFase2(); // Será implementado amanhã
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
      qtdRefeicoes++; // Adiciona 1 na conta
      
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
      faseAtual = 99;           // Muda o estado principal para 99 (Espera Coringa)
        
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
// Mantidas aqui vazias apenas para o código não dar erro de compilação hoje.

void atualizarTelaFase2() {
  lcd.clear();
  lcd.print("Fase 2 em breve");
}

void atualizarTelaFase3() {
  // Vazio
}