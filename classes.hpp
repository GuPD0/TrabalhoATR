#ifndef CLASSES_HPP
#define CLASSES_HPP

#include <vector>
#include <mutex>
#include <numeric>
#include <variant>
#include <algorithm>
#include <condition_variable>

// Struct para os dados processados
struct DadosProcessados {
    int id; // Campo único para identificar o item
    int posicao_x;
    int posicao_y;
    int angulo_x;
};

// Define os tipos possíveis no buffer (adicione mais se precisar)
using DataVariant = std::variant<DadosProcessados, int, std::string>;

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
    BufferCircular(size_t cap) : capacity(cap), inicio(0), fim(0), count(0), buffer(cap) {}

    // Adiciona um item ao buffer (bloqueia se cheio),item pode ser qualquer tipo do variant
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

    // Busca um item por ID (apenas se for DadosProcessados; lança exception se não encontrar)
    DataVariant get(int id) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = std::find_if(buffer.begin(), buffer.end(), [id](const DataVariant& item) {
            // Verifica se e DadosProcessados e se o id bate
            if (std::holds_alternative<DadosProcessados>(item)) {
                return std::get<DadosProcessados>(item).id == id;
            }
            return false;
        });
        if (it != buffer.end()) {
            return *it; // Retorna copia
        }
        throw std::out_of_range("ID não encontrado no buffer");
    }

    // Remove um item por ID (apenas se for DadosProcessados)
    bool removeById(int id) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = std::find_if(buffer.begin(), buffer.end(), [id](const DataVariant& item) {
            if (std::holds_alternative<DadosProcessados>(item)) {
                return std::get<DadosProcessados>(item).id == id;
            }
            return false;
        });
        if (it != buffer.end()) {
            buffer.erase(it);
            --count;
            cv_not_full.notify_one();
            return true;
        }
        return false;
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
