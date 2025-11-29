#include "controle_navegacao.hpp"
#include <iostream>
#include <cmath>
#include <chrono>
#include <thread>
#include <atomic>
#include <algorithm>

// --- EXTERNOS que devem existir no projeto ---
extern std::atomic<bool> running;

// Flags de falha
extern std::atomic<bool> falha_temperatura;
extern std::atomic<bool> falha_eletrica;
extern std::atomic<bool> falha_hidraulica;

// Ganhos
static double KP_VEL = 2.0;
static double KP_ANG = 3.0;
static double KD_ANG = 0.2;

static double normalize_deg(double a) {
    while (a > 180.0) a -= 360.0;
    while (a < -180.0) a += 360.0;
    return a;
}

static constexpr int DEFAULT_TRUCK_ID = 1;

void ControleDeNavegacao(BufferCircular& buf) {

    bool controller_enabled = false;
    bool teve_defeito = false;

    double set_x = 0.0, set_y = 0.0, set_ang = 0.0;

    double last_pos_x = 0.0, last_pos_y = 0.0, last_ang = 0.0;
    bool have_position = false;

    double vel_limit = 100.0;

    double last_ang_error = 0.0;

    while (running.load()) {

        // Falhas
        if (falha_temperatura.exchange(false) ||
            falha_eletrica.exchange(false) ||
            falha_hidraulica.exchange(false))
        {
            teve_defeito = true;
            controller_enabled = false;

            Atuadores a0;
            a0.id = DEFAULT_TRUCK_ID;
            a0.aceleracao = 0;
            a0.direcao = 0;
            buf.push(a0);

            std::cout << "[Controle] Falha -> atuadores zerados\n";
        }

        // Leitura do buffer
        DataVariant item = buf.pop();

        // Sensores
        if (std::holds_alternative<DadosSensores>(item)) {
            auto s = std::get<DadosSensores>(item);

            last_pos_x = s.pos_x;
            last_pos_y = s.pos_y;
            last_ang   = s.angulo;
            have_position = true;

            // Controle automático
            if (controller_enabled && !teve_defeito) {

                double atual_ang = s.angulo;

                // -------------- USANDO O SETPOINT ANGULAR DO PLANEJAMENTO --------------
                double err_ang = normalize_deg(set_ang - atual_ang);
                double p = KP_ANG * err_ang;
                double d = KD_ANG * (err_ang - last_ang_error);
                last_ang_error = err_ang;
                double dir_cmd = normalize_deg(p + d);

                if (dir_cmd > 180) dir_cmd = 180;
                if (dir_cmd < -180) dir_cmd = -180;

                // velocidade limitada
                double acel = vel_limit;
                if (acel < 0) acel = 0;
                if (acel > 100) acel = 100;

                Atuadores atu;
                atu.id = DEFAULT_TRUCK_ID;
                atu.aceleracao = (int)std::round(acel);
                atu.direcao = (int)std::round(dir_cmd);

                buf.push(atu);

                // Log
                std::cout << "[Controle] AUTO: vel=" << atu.aceleracao
                          << " ang=" << atu.direcao << "\n";
            }

            continue;
        }

        // Comandos Operador
        else if (std::holds_alternative<ComandoOperador>(item)) {
            auto c = std::get<ComandoOperador>(item);

            switch (c.comando) {
                case TipoComando::SET_MANUAL:
                    if (have_position) {
                        set_x = last_pos_x;
                        set_y = last_pos_y;
                        set_ang = last_ang;
                        std::cout << "[Controle] SET_MANUAL -> bumpless\n";
                    }
                    controller_enabled = false;
                    break;

                case TipoComando::SET_AUTOMATICO:
                    if (!teve_defeito) {
                        controller_enabled = true;
                        std::cout << "[Controle] SET_AUTOMATICO -> habilitado\n";
                    }
                    break;

                case TipoComando::REARME_FALHA:
                    teve_defeito = false;
                    std::cout << "[Controle] Rearme executado\n";
                    break;

                default:
                    break;
            }
            continue;
        }

        // Setpoints do Planejamento
        else if (std::holds_alternative<DadosProcessados>(item)) {
            auto p = std::get<DadosProcessados>(item);

            if (p.id == 999) {
                vel_limit = std::clamp((double)p.posicao_y, 0.0, 100.0);
                set_ang   = p.angulo_x;

                std::cout << "[Controle] Setpoint Planejamento: vel="
                          << vel_limit << " ang=" << set_ang << "\n";
            }

            continue;
        }
    }

    std::cout << "[Controle] Encerrando thread\n";
}
