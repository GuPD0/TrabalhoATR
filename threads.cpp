#include <iostream>
#include <map>
#include <thread>
#include <chrono>
#include <atomic>
#include "threads.hpp"
#include "mqtt.hpp"

extern std::atomic<bool> running;
std::atomic<bool> falha_temperatura{false};
std::atomic<bool> falha_eletrica{false};
std::atomic<bool> falha_hidraulica{false};

// ===================================================================
// MONITORAMENTO DE FALHAS
// ===================================================================
// Estrutura para falhas por caminhão
struct FalhasStatus {
    std::atomic<bool> temperatura{false};
    std::atomic<bool> eletrica{false};
    std::atomic<bool> hidraulica{false};
};
// Mapa global compartilhado entre thread MQTT e thread de monitoramento
std::map<int, FalhasStatus> falhas_caminhoes;
std::mutex falhas_mutex;
void MonitoramentoDeFalhas() {
    while (true) {
        {
            std::lock_guard<std::mutex> lock(falhas_mutex);
            for (const auto& [truck_id, status] : falhas_caminhoes) {
                if (status.temperatura.load()) {
                    std::cout << "MonitoramentoDeFalhas: Falha de Temperatura detectada no caminhão " << truck_id << std::endl;
                }
                if (status.eletrica.load()) {
                    std::cout << "MonitoramentoDeFalhas: Falha Elétrica detectada no caminhão " << truck_id << std::endl;
                }
                if (status.hidraulica.load()) {
                    std::cout << "MonitoramentoDeFalhas: Falha Hidráulica detectada no caminhão " << truck_id << std::endl;
                }
            }
        }
        // Aguarda antes da próxima checagem para evitar uso excessivo de CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

