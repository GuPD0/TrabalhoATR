# 🚚 Trabalho Final – Automação em Tempo Real (ATR)

Este repositório contém a implementação de um **caminhão autônomo simulado** que se comunica com a **Simulação da Mina** via **MQTT**, processa sensores, monitora falhas e executa lógica de comando conforme especificação da disciplina **Automação em Tempo Real**.

> **Status: Em desenvolvimento – integração completa ainda não testada**

---

# 📌 Estrutura do Projeto

```
TrabalhoATR/
│
├── main.cpp                  → Inicialização do sistema e controle das threads
├── threads.cpp               → Tarefas de tempo real: sensores, falhas, lógica
├── threads.hpp               → Declarações das tarefas e variável global running
├── classes.hpp               → Definições das estruturas de dados e buffer circular
├── mqtt.cpp                  → Comunicação MQTT (sensores + atuadores)
├── mqtt.hpp                  → Declarações das funções MQTT
├── SimulacaoDaMina.cpp       → Simulação fornecida (executada separadamente)
└── interface.py              → Interface local (opcional, ainda em evolução)
```

---

# 🚀 Funcionalidades Implementadas

## ✔ Comunicação MQTT
- Recebimento de todos os sensores da simulação:
  - `i_posicao_x`, `i_posicao_y`, `i_angulo`
  - `i_temperatura`
  - `i_falha_eletrica`, `i_falha_hidraulica`
- Envio dos atuadores para a simulação:
  - `o_aceleracao`
  - `o_direcao`
- Parser completo + push no BufferCircular.

## ✔ Tratamento de Sensores
- Filtro de média móvel (ordem 5) aplicado a X, Y e ângulo.
- Impressão de sensores filtrados.

## ✔ Monitoramento de Falhas
Conforme especificação do trabalho:
- Temperatura > 95°C → **alerta**
- Temperatura > 120°C → **falha crítica**
- Falhas elétrica/hidráulica detectadas.
- Geração de `FalhaEvento` enviado ao buffer.

## ✔ Lógica de Comando
- Controle total de **modo manual/automático**.
- Travamento por falha crítica (`e_defeito = true`).
- Rearme de falhas.
- Aceleração e direção baseadas nos comandos do operador.
- Publicação via MQTT dos atuadores.

## ✔ Encerramento Seguro (SIGINT)
- `Ctrl+C` encerra todas as threads de forma limpa.
- Threads sincronizadas via `std::atomic<bool> running`.

---

# 🧪 Como Executar (Simulação + Caminhão)

## **1. Iniciar a simulação (terminal 1)**
```bash
./simulacao
```

## **2. Adicionar um caminhão à simulação (terminal 2)**
```bash
echo "add_truck" | nc localhost 8080
```

## **3. Compilar e executar o caminhão (terminal 3)**
```bash
g++ -std=c++17 -pthread main.cpp threads.cpp mqtt.cpp -lmosquitto -lboost_system -o caminhao
./caminhao
```

O caminhão deve:
- Receber e imprimir sensores
- Filtrar posições
- Detectar falhas
- Executar lógica
- Publicar comandos de direção/aceleração
- Movimentar-se na simulação

---

# 📌 O que ainda falta implementar

## 🔧 1. Testes completos com a simulação
- Validar movimentação do caminhão
- Testar falhas e rearme
- Testar estados do modo automático/manual

## 🔧 2. Processamento completo de `DadosProcessados`
- Tratamento real alinhado ao PDF do trabalho
- Normalização e cálculo adicional

## 🔧 3. Generalização do ID do caminhão
Atualmente:
```cpp
publicar_atuadores(1, aceleracao, direcao);
```
Precisa virar:
```cpp
./caminhao 3    # por exemplo
```

## 🔧 4. Integração com a Interface Local (Python/Flet)
- Enviar comandos reais para o caminhão via socket/MQTT
- Mostrar estado do caminhão
- Mostrar alertas e falhas

## 🔧 5. Limpeza e documentação final
- Comentários
- Diagrama de arquitetura
- Padronização
- README detalhado

---

# 📚 Referências
- Especificação oficial do Trabalho Final ATR
- Documentação MQTT/Mosquitto
- Material da disciplina (Sincronização, IPC, Threads, Tempo Real)

---

# 👩‍💻 Autores
Projeto desenvolvido por **Gustavo, Lariane e Lucas** — Engenharia de Controle e Automação (UFMG).

---
