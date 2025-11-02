#ifndef CLASSES_HPP
#define CLASSES_HPP

#include <vector>
#include <mutex>
#include <numeric>
#include <variant>
#include <algorithm>
#include <condition_variable>
#include <string>

// Struct para os dados processados
struct DadosProcessados {
    int id; // Campo único para identificar o item
    int posicao_x;
    int posicao_y;
    int angulo_x;
};

// INÍCIO MONITORAMENTO DE FALHAS
// Enum para definir os tipos de falha de forma segura e legível.
// Isso evita o uso de números ou strings, prevenindo erros.
enum class TipoFalha {
    OK,                 // Nenhum defeito detectado.
    Eletrica,           // Falha no sistema elétrico (i_falha_eletrica).
    Hidraulica,         // Falha no sistema hidráulico (i_falha_hidraulica).
    TemperaturaAlerta,  // Temperatura acima do nível de alerta (T > 95°C).
    TemperaturaCritica  // Temperatura acima do nível crítico, gerando defeito (T > 120°C).
};

// Struct para encapsular as informações de um evento de falha.
// Este é o "evento" que será disparado pela tarefa de monitoramento.
struct FalhaEvento {
    TipoFalha tipo;         // O tipo da falha, usando o enum definido acima.
    std::string descricao;  // Uma descrição textual da falha para logging e display.
};
// FIM MONITORAMENTO DE FALHAS

// INÍCIO LÓGICA DE COMANDO
// Enum para representar os comandos do operador, conforme Tabela 2.
// Usar um enum torna o código mais seguro e fácil de ler.
enum class TipoComando {
    SET_AUTOMATICO, // Comando c_automatico
    SET_MANUAL,     // Comando c_man
    REARME_FALHA,   // Comando c_rearme
    ACELERA,        // Comando c_acelera
    GIRA_DIREITA,   // Comando c_direita
    GIRA_ESQUERDA   // Comando c_esquerda
};

// Struct para representar um comando vindo da Interface Local.
// Quando a Interface Local for implementada, ela enviará objetos deste tipo.
struct ComandoOperador {
    TipoComando comando; // O tipo de comando que o operador executou.
};

// Struct para armazenar o estado atual do veículo, conforme Tabela 2.
// A tarefa Lógica de Comando irá manter e atualizar um objeto deste tipo.
struct EstadoVeiculo {
    // Representa a variável e_defeito. 'true' se há defeito, 'false' se não há.
    // Inicia como 'false' (sem defeito).
    bool e_defeito = false;

    // Representa a variável e_automatico. 'true' se está em modo automático, 'false' se manual.
    // Inicia como 'false' (modo manual, que é mais seguro).
    bool e_automatico = false;
};
// FIM LÓGICA DE COMANDO

// Define os tipos possíveis no buffer (adicione mais se precisar)
using DataVariant = std::variant<DadosProcessados, FalhaEvento, ComandoOperador>;

class BufferCircular {
private:
    std::vector<DataVariant> buffer; // Agora armazena tipos mistos
    size_t capacity;
    size_t inicio; // indice para leitura
    size_t fim; // indice para escrita
    size_t count; // Numero de elementos no buffer
    std::mutex mtx;
    std::condition_variable cv_not_empty;
    std::condition_variable cv_not_full;

public:
    // Construtor com capacidade fixa em 200 (tamanho máximo)
    BufferCircular() : capacity(200), inicio(0), fim(0), count(0), buffer(200) {}

    // Adiciona um item ao buffer (bloqueia se cheio), item pode ser qualquer tipo do variant
    void push(DataVariant item) {
        std::unique_lock<std::mutex> lock(mtx);
        cv_not_full.wait(lock, [this]() { return count < capacity; });
        buffer[fim] = item;
        fim = (fim + 1) % capacity;
        ++count;
        cv_not_empty.notify_one();
    }

    // Remove um item do buffer (bloqueia se vazio) - FIFO
    DataVariant pop() {
        std::unique_lock<std::mutex> lock(mtx);
        cv_not_empty.wait(lock, [this]() { return count > 0; });
        DataVariant item = buffer[inicio];
        inicio = (inicio + 1) % capacity;
        --count;
        cv_not_full.notify_one();
        return item;
    }

    // Verifica se está vazio
    bool isEmpty() {
        std::lock_guard<std::mutex> lock(mtx);
        return count == 0;
    }

    // Verifica se está cheio
    bool isFull() {
        std::lock_guard<std::mutex> lock(mtx);
        return count == capacity;
    }
};

// Classe para filtro de média móvel
class FiltroMediaMovel {
private:
    std::vector<int> amostras;
    size_t ordem;

public:
    FiltroMediaMovel(size_t m) : ordem(m) {}

    int filtrar(int nova_amostra) {
        if (amostras.size() < ordem) {
            amostras.push_back(nova_amostra);
        } else {
            amostras.erase(amostras.begin());
            amostras.push_back(nova_amostra);
        }
        return std::accumulate(amostras.begin(), amostras.end(), 0) / amostras.size();
    }
};

#endif
