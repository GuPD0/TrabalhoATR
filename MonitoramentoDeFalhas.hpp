#ifndef THREADS_HPP
#define THREADS_HPP

#include <atomic>
#include <map>
#include <mutex>

// Flags de falha (via eventos)
extern std::atomic<bool> falha_temperatura;
extern std::atomic<bool> falha_eletrica;
extern std::atomic<bool> falha_hidraulica;

// Estado das falhas por caminhão
struct FalhasStatus {
    std::atomic<bool> temperatura{false};
    std::atomic<bool> eletrica{false};
    std::atomic<bool> hidraulica{false};
};

// Mapa global caminhão -> status de falhas
extern std::map<int, FalhasStatus> falhas_caminhoes;
extern std::mutex falhas_mutex;

// Thread de monitoramento de falhas
void MonitoramentoDeFalhas();

#endif
