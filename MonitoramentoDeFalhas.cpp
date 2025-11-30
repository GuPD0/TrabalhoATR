#include "MonitoramentoDeFalhas.hpp"
#include "mqtt.hpp"

#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>

// Flags globais (eventos)
std::atomic<bool> falha_temperatura{false};
std::atomic<bool> falha_eletrica{false};
std::atomic<bool> falha_hidraulica{false};

// Estado por caminhão
std::map<int, FalhasStatus> falhas_caminhoes;
std::mutex falhas_mutex;

// running vem do main.cpp
extern std::atomic<bool> running;

/* --------------------------------------------------------------------
   MONITORAMENTO DE FALHAS
   - NÃO utiliza buffer
   - Lê sensoriamento via MQTT (callback atualiza falhas_caminhoes)
   - Dispara eventos para:
        → LógicaDeComando
        → ControleDeNavegacao
        → ColetorDeDados
   -------------------------------------------------------------------- */

void MonitoramentoDeFalhas() {
    std::cout << "[MonitoramentoDeFalhas] Thread iniciada.\n";
    while (running.load()) {
        {
            std::lock_guard<std::mutex> lock(falhas_mutex);
            for (auto& [id, status] : falhas_caminhoes) {
                // Temperatura
                if (status.temperatura.load()) {
                    falha_temperatura = true;
                    std::cout << "MonitoramentoDeFalhas: Falha de Temperatura detectada no caminhão " << id << "\n";
                    status.temperatura.store(false); // reseta após notificar
                }
                // Elétrica
                if (status.eletrica.load()) {
                    falha_eletrica = true;
                    std::cout << "MonitoramentoDeFalhas: Falha Elétrica detectada no caminhão " << id << "\n";
                    status.eletrica.store(false); // reseta após notificar
                }
                // Hidráulica
                if (status.hidraulica.load()) {
                    falha_hidraulica = true;
                    std::cout << "MonitoramentoDeFalhas: Falha Hidráulica detectada no caminhão " << id << "\n";
                    status.hidraulica.store(false); // reseta após notificar
                }
            }
        }
        // Verifica a cada 100 ms
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "[MonitoramentoDeFalhas] Encerrando... \n";
}
