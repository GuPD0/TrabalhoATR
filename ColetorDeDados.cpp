#include "ColetorDeDados.hpp"
#include "classes.hpp"
#include "threads.hpp"
#include "mqtt.hpp"

#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <map>
#include <mutex>

extern std::atomic<bool> running;

// flags globais de falha (já existentes no sistema)
extern std::map<int, FalhasStatus> falhas_caminhoes;
extern std::mutex falhas_mutex;

// ======================================================================
//  COLETOR DE DADOS — COMPORTAMENTO REAL (PDF)
// ======================================================================
//
//  Lê todos os eventos do buffer + estados de falha.
//  Gera um log em arquivo com timestamp.
//  Futuramente: envia estados via IPC para Interface Local.
//
void ColetorDeDados(BufferCircular& buf)
{
    std::ofstream logfile("log_caminhao.txt", std::ios::app);

    if (!logfile.is_open()) {
        std::cerr << "[ColetorDeDados] ERRO: não foi possível abrir arquivo de log!" << std::endl;
        return;
    }

    while (running.load()) {

        // 1. LER EVENTOS DO BUFFER -----------------------------------------
        DataVariant item = buf.pop();

        auto now = std::chrono::system_clock::now();
        std::time_t ts = std::chrono::system_clock::to_time_t(now);

        logfile << "[" << ts << "] ";

        // ------------------------- SENSORES ---------------------------
        if (std::holds_alternative<DadosSensores>(item)) {
            auto s = std::get<DadosSensores>(item);

            logfile << "SENSORES | "
                    << "X=" << s.pos_x
                    << " Y=" << s.pos_y
                    << " ANG=" << s.angulo
                    << " TEMP=" << s.temperatura
                    << " FE=" << s.falha_eletrica
                    << " FH=" << s.falha_hidraulica
                    << "\n";
        }

        // ------------------------- FALHAS -----------------------------
        else if (std::holds_alternative<FalhaEvento>(item)) {
            auto f = std::get<FalhaEvento>(item);

            logfile << "FALHA | "
                    << f.descricao
                    << "\n";
        }

        // ------------------------- PROCESSADOS ------------------------
        else if (std::holds_alternative<DadosProcessados>(item)) {
            auto p = std::get<DadosProcessados>(item);

            logfile << "PROCESSADO | ID=" << p.id
                    << " X=" << p.posicao_x
                    << " Y=" << p.posicao_y
                    << " ANG=" << p.angulo_x
                    << "\n";
        }

        // ------------------------- COMANDOS ---------------------------
        else if (std::holds_alternative<ComandoOperador>(item)) {
            auto c = std::get<ComandoOperador>(item);

            logfile << "COMANDO | Tipo=" << (int)c.comando << "\n";
        }

        logfile.flush();

        // ------------------------------------------------------------------
        // 2. VERIFICAR FLAGS DE FALHA DAS OUTRAS THREADS (via eventos)
        // ------------------------------------------------------------------
        {
            std::lock_guard<std::mutex> lock(falhas_mutex);

            for (auto& [id, status] : falhas_caminhoes) {

                if (status.falha_temperatura.exchange(false)) {
                    logfile << "[" << ts << "] EVENTO | Caminhao " << id
                            << " | Falha de Temperatura\n";
                }

                if (status.falha_eletrica.exchange(false)) {
                    logfile << "[" << ts << "] EVENTO | Caminhao " << id
                            << " | Falha Elétrica\n";
                }

                if (status.falha_hidraulica.exchange(false)) {
                    logfile << "[" << ts << "] EVENTO | Caminhao " << id
                            << " | Falha Hidráulica\n";
                }
            }
        }

        //std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    logfile.close();
}