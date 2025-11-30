#ifndef MONITORAMENTO_DE_FALHAS_HPP
#define MONITORAMENTO_DE_FALHAS_HPP

#include <atomic>
#include <map>
#include <mutex>

// Flags de falha (via eventos)
extern std::atomic<bool> falha_temperatura;
extern std::atomic<bool> falha_eletrica;
extern std::atomic<bool> falha_hidraulica;

// Status de cada caminhão (recebido do MQTT)
struct FalhasStatus {
    std::atomic<bool> temperatura{false};
    std::atomic<bool> eletrica{false};
    std::atomic<bool> hidraulica{false};
};

// Mapa global caminhão -> status de falhas
extern std::mutex falhas_mutex;
extern std::map<int, FalhasStatus> falhas_caminhoes;

// Thread de monitoramento de falhas
void MonitoramentoDeFalhas();

#endif
