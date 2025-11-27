#include <iostream>
#include <map>
#include <thread>
#include <chrono>
#include <atomic>
#include "threads.hpp"
#include "mqtt.hpp"

extern std::atomic<bool> running;

// ===================================================================
// TRATAMENTO DE SENSORES
// ===================================================================
void TratamentoSensores(BufferCircular& buf) {
    FiltroMediaMovel filtro_x(5);
    FiltroMediaMovel filtro_y(5);
    FiltroMediaMovel filtro_ang(5);

    while (running.load()) {

        // Aguarda item do buffer
        DataVariant item = buf.pop();

        // Só processa DadosSensores
        if (std::holds_alternative<DadosSensores>(item)) {
            DadosSensores dados = std::get<DadosSensores>(item);

            // Aplica filtros
            int x_filtrado   = filtro_x.filtrar(dados.pos_x);
            int y_filtrado   = filtro_y.filtrar(dados.pos_y);
            int ang_filtrado = filtro_ang.filtrar(dados.angulo);

            // Exibe resultado
            std::cout << "[TratamentoSensores] "
                      << "X=" << x_filtrado
                      << " Y=" << y_filtrado
                      << " ANG=" << ang_filtrado
                      << " TEMP=" << dados.temperatura
                      << " FE=" << dados.falha_eletrica
                      << " FH=" << dados.falha_hidraulica
                      << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "[TratamentoSensores] encerrando..." << std::endl;
}

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

// ===================================================================
// LÓGICA DE COMANDO
// ===================================================================
void LogicaDeComando(BufferCircular& buf) {

    EstadoVeiculo estado;
    int aceleracao = 0;
    int direcao = 0;

    while (running.load()) {

        DataVariant item = buf.pop();

        // ------------------------------ 
        // 1. PROCESSAR EVENTOS DE SENSOR
        // ------------------------------
        if (std::holds_alternative<DadosProcessados>(item)) {
            auto dados = std::get<DadosProcessados>(item);

            // Se estiver no modo automático e NÃO houver defeito
            if (estado.e_automatico && !estado.e_defeito) {
                // Controle automático simples
                aceleracao = 20;    // aceleração leve
                direcao = 0;        // reto

                std::cout << "[LogicaDeComando] AUTO: acelerando..." << std::endl;
            }
        }

        // ------------------------------
        // 2. PROCESSAR EVENTOS DE FALHA
        // ------------------------------
        else if (std::holds_alternative<FalhaEvento>(item)) {
            auto falha = std::get<FalhaEvento>(item);

            if (falha.tipo == TipoFalha::TemperaturaCritica ||
                falha.tipo == TipoFalha::Eletrica ||
                falha.tipo == TipoFalha::Hidraulica) 
            {
                estado.e_defeito = true;
                aceleracao = 0;      // para o caminhão imediatamente
                direcao = 0;

                std::cout << "[LogicaDeComando] FALHA GRAVE → caminhão travado!" << std::endl;
            }

            if (falha.tipo == TipoFalha::TemperaturaAlerta) {
                std::cout << "[LogicaDeComando] ALERTA de temperatura" << std::endl;
            }
        }

        // --------------------------------
        // 3. PROCESSAR COMANDOS DO OPERADOR
        // --------------------------------
        else if (std::holds_alternative<ComandoOperador>(item)) {
            auto cmd = std::get<ComandoOperador>(item);

            switch (cmd.comando) {
                
            case TipoComando::SET_MANUAL:
                if (!estado.e_defeito) {
                    estado.e_automatico = false;
                    std::cout << "[LogicaDeComando] MODO MANUAL" << std::endl;
                }
                break;

            case TipoComando::SET_AUTOMATICO:
                if (!estado.e_defeito) {
                    estado.e_automatico = true;
                    std::cout << "[LogicaDeComando] MODO AUTOMÁTICO" << std::endl;
                }
                break;

            case TipoComando::ACELERA:
                if (!estado.e_defeito && !estado.e_automatico) {
                    aceleracao += 10;
                    if (aceleracao > 100) aceleracao = 100;
                    std::cout << "[LogicaDeComando] MANUAL: acelerando" << std::endl;
                }
                break;

            case TipoComando::GIRA_DIREITA:
                if (!estado.e_defeito && !estado.e_automatico) {
                    direcao += 10;
                    if (direcao > 180) direcao = 180;
                    std::cout << "[LogicaDeComando] MANUAL: virando direita" << std::endl;
                }
                break;

            case TipoComando::GIRA_ESQUERDA:
                if (!estado.e_defeito && !estado.e_automatico) {
                    direcao -= 10;
                    if (direcao < -180) direcao = -180;
                    std::cout << "[LogicaDeComando] MANUAL: virando esquerda" << std::endl;
                }
                break;

            case TipoComando::REARME_FALHA:
                estado.e_defeito = false;
                std::cout << "[LogicaDeComando] REARME EXECUTADO" << std::endl;
                break;

            default:
                break;
            }
        }

        // ---------------------------
        // Debug do estado
        // ---------------------------
        std::cout << "[Estado] AUTO=" << estado.e_automatico
                  << " DEF=" << estado.e_defeito
                  << " ACC=" << aceleracao
                  << " DIR=" << direcao
                  << std::endl;
        
        // ------------------------------
        // Publicar atuadores via MQTT
        // ------------------------------
        publicar_atuadores(1, aceleracao, direcao);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "[LogicaDeComando] encerrando..." << std::endl;
}

