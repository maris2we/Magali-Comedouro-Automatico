#include <Servo.h>

/*
  COMEDOURO — SERVO 360° / Porcionameto

  ENTRADAS DA FUNÇÃO:
  - comando: true solicita o início da alimentação.
  - porcoes: quantidade de porções daquele horário.

  FUNCIONAMENTO:
  O motor gira durante:
      TEMPO_POR_PORCAO × porcoes
  Depois recebe o sinal de parada.

  INTEGRAÇÃO COM O Relógio:
  1. Substitua as variáveis provisórias do loop pelos dados do relógio.
  2. Informe a quantidade junto com o comando de início.
  3. Chame controlarMotor() em todas as repetições do loop,
     mesmo quando o comando estiver false.
  4. Para uma nova alimentação, o comando precisa voltar a false
     e depois mudar para true.

  CUIDADOS:
  - Não usa delay(), permitindo executar outras tarefas.
  - Evite delay() nas outras funções, pois atrasaria a parada.

  AJUSTES:
  - TEMPO_POR_PORCAO: medir na prática com a ração.
  - PULSO_GIRAR: ajustar sentido e velocidade.
  - PULSO_PARAR: ajustar até o servo ficar imóvel.

  LIGAÇÃO:
  - Sinal do servo no pino 9.
  - Alimentação adequada ao servo, com GND comum ao Arduino.

  Se o projeto já tiver setup() e loop(), incorpore o conteúdo
  nas funções existentes, sem criar outras com o mesmo nome.
*/

Servo motor;

const byte PINO_SERVO = 9;

const int PULSO_PARAR = 1500; // Microssegundos.
const int PULSO_GIRAR = 1700;
const unsigned long TEMPO_POR_PORCAO = 2000; // Milissegundos.

bool funcionando = false;
bool comandoAnterior = false;

unsigned long inicio = 0;
unsigned long duracao = 0;

void controlarMotor(bool comando, uint8_t porcoes) {
  unsigned long agora = millis();

  // Detecta a mudança de false para true.
  bool novoComando = comando && !comandoAnterior;
  comandoAnterior = comando;

  if (novoComando && !funcionando && porcoes > 0) {
    duracao = TEMPO_POR_PORCAO * (unsigned long) porcoes;
    inicio = agora;
    funcionando = true;

    motor.writeMicroseconds(PULSO_GIRAR);
  }

  // Encerra o ciclo quando atingir o tempo calculado.
  if (funcionando && agora - inicio >= duracao) {
    motor.writeMicroseconds(PULSO_PARAR);
    funcionando = false;
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(11, INPUT_PULLUP);

  motor.writeMicroseconds(PULSO_PARAR);
  motor.attach(PINO_SERVO);
}

void loop() {
  bool pressionado = digitalRead(11) == LOW;

  // Mostra no Monitor Serial se o botão está sendo reconhecido.
  static bool anterior = false;

  if (pressionado != anterior) {
    Serial.println(pressionado ? "BOTAO APERTADO" : "BOTAO SOLTO");
    anterior = pressionado;
  }

  controlarMotor(pressionado, 1);
}