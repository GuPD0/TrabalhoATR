#ifndef THREADS_HPP
#define THREADS_HPP

#include <atomic>
#include "classes.hpp"

// Flags de falha (via eventos)
extern std::atomic<bool> falha_temperatura;
extern std::atomic<bool> falha_eletrica;
extern std::atomic<bool> falha_hidraulica;

struct FalhasStatus {
    std::atomic<bool> temperatura{false};
    std::atomic<bool> eletrica{false};
    std::atomic<bool> hidraulica{false};
    std::atomic<bool> falha_temperatura{false};
    std::atomic<bool> falha_eletrica{false};
    std::atomic<bool> falha_hidraulica{false};
};

extern std::map<int, FalhasStatus> falhas_caminhoes;
extern std::mutex falhas_mutex;

// Declarações das funções das threads
void TratamentoSensores(BufferCircular& buf);
void MonitoramentoDeFalhas(BufferCircular& buf);
void LogicaDeComando(BufferCircular& buf);

#endif
