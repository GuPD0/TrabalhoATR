#include "controle_navegacao.hpp"
#include <iostream>
#include <cmath>
#include <chrono>
#include <thread>
#include <atomic>

// --- EXTERNOS que devem existir no projeto ---
// 1) Flags (eventos) que o MonitoramentoDeFalhas setará:
//    defina-as em algum arquivo .cpp (por exemplo em main.cpp ou threads.cpp)
extern std::atomic<bool> falha_temperatura;
extern std::atomic<bool> falha_eletrica;
extern std::atomic<bool> falha_hidraulica;

// Observação: se preferir usar condition_variable para despertar o controlador,
// substitua as atomics por uma condvar+mutex; aqui optamos por atomics simples.

// -------------------------------------------------
// Ganhos do controlador (ajuste empírico depois)
static double KP_VEL = 2.0;   // ganho proporcional da velocidade (mapear para -100..100)
static double KP_ANG = 3.0;   // ganho proporcional ângulo
static double KD_ANG = 0.2;   // ganho derivativo ângulo

// Normaliza ângulo em graus para [-180,180]
static double normalize_deg(double a) {
    while (a > 180.0) a -= 360.0;
    while (a < -180.0) a += 360.0;
    return a;
}

// Se seu DadosSensores tiver campo id, preferir usar esse id.
// Se ainda não tiver, atuadores serão publicados com id=1 (placeholder).
static constexpr int DEFAULT_TRUCK_ID = 1;

// Comportamento:
// - Lê do buffer: DadosSensores, DadosProcessados (opcional) e ComandoOperador.
// - Se em MANUAL: faz bumpless transfer (setpoint = posição atual) e DESLIGA controlador.
// - Se em AUTOMÁTICO: calcula comandos (vel + direção) e empurra Atuadores no buffer.
// - Se receber evento de falha crítica (atomics), desliga e envia atuadores zero (segurança).
void ControleDeNavegacao(BufferCircular& buf) {
    // estado local
    bool controller_enabled = false;
    bool teve_defeito = false;

    // bumpless setpoints e estado sensorial
    double set_x = 0.0, set_y = 0.0, set_ang = 0.0;
    bool have_position = false;

    // último erro angular para termo derivativo
    double last_ang_error = 0.0;

    // loop principal
    while (true) {
        // 1) monitorar eventos de falha (sempre checar primeiro, sem depender do buffer)
        if (falha_temperatura.load() || falha_eletrica.load() || falha_hidraulica.load()) {
            // evento crítico: desabilitar controlador
            teve_defeito = true;
            controller_enabled = false;
            // publicar atuadores zero para segurança (id temporário)
            // NOTE: precisaremos do tipo Atuadores no DataVariant (veja instruções abaixo)
            Atuadores a_zero;
            a_zero.id = DEFAULT_TRUCK_ID;
            a_zero.aceleracao = 0;
            a_zero.direcao = 0;
            // push seguro: se buffer cheio, o push bloqueará até espaço
            buf.push(a_zero);

            std::cout << "[Controle] Falha detectada via evento -> controlador desligado e atuadores zerados\n";

            // limpar as flags de evento (opcional - ou deixe o monitor limpar)
            // falha_temperatura.store(false); // NÃO limpe aqui se o monitor quiser manter
        }

        // 2) ler próximo item do buffer (bloqueante)
        DataVariant item = buf.pop();

        // 2a) Sensores
        if (std::holds_alternative<DadosSensores>(item)) {
            DadosSensores s = std::get<DadosSensores>(item);
            // se seu DadosSensores tiver campo id, capture-o; aqui demo sem id
            set_x = set_x; // nada por enquanto
            have_position = true;

            // armazenar posição atual para uso quando receber comando MANUAL (bumpless)
            // usamos valores diretamente quando necessário
            double atual_x = static_cast<double>(s.pos_x);
            double atual_y = static_cast<double>(s.pos_y);
            double atual_ang = static_cast<double>(s.angulo);

            // se controller foi habilitado e nao houve defeito, controle abaixo
            if (controller_enabled && !teve_defeito) {
                // cálculo simples:
                // erro posição -> magnitude para velocidade
                double err_x = set_x - atual_x;
                double err_y = set_y - atual_y;
                double dist = std::hypot(err_x, err_y);

                // aceleração proporcional (saturar 0..100)
                double acel = KP_VEL * dist;
                if (acel > 100.0) acel = 100.0;

                // cálculo ângulo desejado
                double desired_ang = std::atan2(err_y, err_x) * 180.0 / M_PI;
                double err_ang = normalize_deg(desired_ang - atual_ang);

                double p = KP_ANG * err_ang;
                double d = KD_ANG * (err_ang - last_ang_error);
                double dir_cmd = p + d;
                last_ang_error = err_ang;

                // saturar direção
                if (dir_cmd > 180.0) dir_cmd = 180.0;
                if (dir_cmd < -180.0) dir_cmd = -180.0;

                int acel_i = static_cast<int>(std::round(acel));
                int dir_i  = static_cast<int>(std::round(normalize_deg(dir_cmd)));

                Atuadores atu;
                atu.id = DEFAULT_TRUCK_ID;
                atu.aceleracao = acel_i;
                atu.direcao = dir_i;
                buf.push(atu); // publica no buffer para LógicaDeComando consumir

                // também opcional: publicar DadosProcessados para Coletor
                DadosProcessados dp;
                dp.id = atu.id;
                dp.posicao_x = s.pos_x;
                dp.posicao_y = s.pos_y;
                dp.angulo_x = s.angulo;
                buf.push(dp);

                std::cout << "[Controle] AUTO -> dist="<<dist<<" acel="<<acel_i<<" dir="<<dir_i<<"\n";
            }

            // se controller desabilitado (manual ou defeito), não produzir atuadores
            continue;
        }

        // 2b) Comando operador (manual/auto/rearme)
        else if (std::holds_alternative<ComandoOperador>(item)) {
            ComandoOperador cmd = std::get<ComandoOperador>(item);
            switch (cmd.comando) {
                case TipoComando::SET_MANUAL:
                    // bumpless: setpoint = pos atual (se disponível)
                    if (have_position) {
                        // assumimos que a posição atual estava gravada em set_x,set_y,set_ang
                        // (ou poderiam vir em último DadosSensores lido)
                        std::cout << "[Controle] RECEBEU SET_MANUAL -> bumpless (setpoints = pos atual)\n";
                    } else {
                        std::cout << "[Controle] RECEBEU SET_MANUAL, mas posição desconhecida\n";
                    }
                    controller_enabled = false;
                    break;

                case TipoComando::SET_AUTOMATICO:
                    if (!teve_defeito) {
                        controller_enabled = true;
                        std::cout << "[Controle] RECEBEU SET_AUTOMATICO -> controlador habilitado\n";
                    } else {
                        std::cout << "[Controle] RECEBEU SET_AUTOMATICO, mas há defeito presente\n";
                    }
                    break;

                case TipoComando::REARME_FALHA:
                    // permitir retorno do defeito (rearme): reseta flag local
                    teve_defeito = false;
                    std::cout << "[Controle] REARME_FALHA -> permissao para reativar controlador\n";
                    break;

                default:
                    break;
            }
            continue;
        }

        // 2c) Setpoint / Planejamento de Rota
        else if (std::holds_alternative<DadosProcessados>(item)) {
            DadosProcessados p = std::get<DadosProcessados>(item);

            // Se o ID for 999, é uma ordem do Planejamento de Rota
            if (p.id == 999) {
                // O Planejamento manda a velocidade desejada no campo posicao_y
                double velocidade_alvo = static_cast<double>(p.posicao_y);
             
                // O Planejamento manda o angulo desejado no campo angulo_x
                double angulo_alvo = static_cast<double>(p.angulo_x);

                // Atualiza setpoints locais do controlador
                // Nota: Você deve ter variáveis como 'set_vel' e 'set_ang' no escopo da função
                set_x = 0; // Não usamos X/Y direto no controle PID simples, usamos erro angular
                set_y = 0; 
             
                // Lógica interna: Aqui você adaptaria para o seu PID usar 'angulo_alvo'
                // Exemplo:
                set_ang = angulo_alvo;
             
                // HACK: Ajustar o KP_VEL dinamicamente ou usar a velocidade enviada como limite
                // std::cout << "[Controle] Setpoint Atualizado: Vel=" << velocidade_alvo << " Ang=" << angulo_alvo << "\n";
        }
        continue;
    }

        // 2d) Caso seja outro tipo, ignoramos (ou tratamos se for o caso)
    }

    // nunca deveria chegar aqui
}
