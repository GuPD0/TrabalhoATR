#include "LogicaDeComando.hpp"
#include "mqtt.hpp"                       // publicar_atuadores
#include "MonitoramentoDeFalhas.hpp"     // declarações de flags e estruturas (se necessário)
#include "classes.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

// externs esperados no projeto (definidos em main / threads.cpp)
extern std::atomic<bool> running;
extern std::atomic<bool> falha_temperatura;
extern std::atomic<bool> falha_eletrica;
extern std::atomic<bool> falha_hidraulica;

// ID padrão (substituir futuramente por parâmetro)
static constexpr int DEFAULT_TRUCK_ID = 1;

void LogicaDeComando(BufferCircular& buf) {
    EstadoVeiculo estado;
    int aceleracao = 0; // -100..100
    int direcao = 0;    // -180..180

    // Armazenar último DadosProcessados lido (se precisar)
    DadosProcessados ultimo_dp;

    // Para evitar repassar evento múltiplas vezes, usamos variáveis locais
    while (running.load()) {

        // 0) Checar eventos de falha vindos por flags/atomics (monitoramento de falhas)
        // Se alguma flag estiver true, cria-se um FalhaEvento e empurra no buffer
        // (assim ColetorDeDados e Controle recebem o evento)
        bool any_fault = false;
        if (falha_temperatura.exchange(false)) {
            any_fault = true;
            FalhaEvento fe;
            fe.tipo = TipoFalha::TemperaturaCritica;
            fe.descricao = "Temperatura crítica detectada (flag).";
            try { buf.push(fe); } catch (...) {}
            estado.e_defeito = true;
            std::cout << "[LogicaDeComando] Evento: Temperatura crítica (flag)\n";
        }
        if (falha_eletrica.exchange(false)) {
            any_fault = true;
            FalhaEvento fe;
            fe.tipo = TipoFalha::Eletrica;
            fe.descricao = "Falha elétrica detectada (flag).";
            try { buf.push(fe); } catch (...) {}
            estado.e_defeito = true;
            std::cout << "[LogicaDeComando] Evento: Falha elétrica (flag)\n";
        }
        if (falha_hidraulica.exchange(false)) {
            any_fault = true;
            FalhaEvento fe;
            fe.tipo = TipoFalha::Hidraulica;
            fe.descricao = "Falha hidráulica detectada (flag).";
            try { buf.push(fe); } catch (...) {}
            estado.e_defeito = true;
            std::cout << "[LogicaDeComando] Evento: Falha hidráulica (flag)\n";
        }
        if (any_fault) {
            // segurança: parar o caminhão imediatamente (publicar via MQTT e enviar Atuadores ao buffer)
            aceleracao = 0;
            direcao = 0;
            publicar_atuadores(DEFAULT_TRUCK_ID, aceleracao, direcao);

            Atuadores atu_zero;
            atu_zero.id = DEFAULT_TRUCK_ID;
            atu_zero.aceleracao = aceleracao;
            atu_zero.direcao = direcao;
            try { buf.push(atu_zero); } catch(...) {}

            // não retornamos aqui; deixamos o loop continuar para processar itens do buffer
        }

        // 1) Ler próximo item do buffer (bloqueante)
        DataVariant item = buf.pop();

        // 2) PROCESSAR tipos recebidos
        // 2.a DadosProcessados -> atualização de estado / decisão automática
        if (std::holds_alternative<DadosProcessados>(item)) {
            auto dp = std::get<DadosProcessados>(item);
            ultimo_dp = dp;

            // NÃO executar controle automático aqui — isso é feita por ControleDeNavegacao.
            // Podemos usar DadosProcessados para decisões de alto nível (logs, validações), por ora apenas log:
            std::cout << "[LogicaDeComando] DadosProcessados recebidos: ID=" << dp.id
                      << " X=" << dp.posicao_x << " Y=" << dp.posicao_y
                      << " ANG=" << dp.angulo_x << "\n";
        }

        // 2.b Evento de Falha (proveniente de Monitoramento de Falhas que escreveu no buffer)
        else if (std::holds_alternative<FalhaEvento>(item)) {
            auto fe = std::get<FalhaEvento>(item);
            // Marca defeito e zera atuadores como medida de segurança
            estado.e_defeito = true;

            aceleracao = 0;
            direcao = 0;
            publicar_atuadores(DEFAULT_TRUCK_ID, aceleracao, direcao);

            Atuadores atu_zero;
            atu_zero.id = DEFAULT_TRUCK_ID;
            atu_zero.aceleracao = aceleracao;
            atu_zero.direcao = direcao;
            try { buf.push(atu_zero); } catch(...) {}

            std::cout << "[LogicaDeComando] FalhaEvento processado: " << fe.descricao << "\n";
        }

        // 2.c Comando do operador (Interface Local)
        else if (std::holds_alternative<ComandoOperador>(item)) {
            auto cmd = std::get<ComandoOperador>(item);
            switch (cmd.comando) {
                case TipoComando::SET_MANUAL:
                    if (!estado.e_defeito) {
                        estado.e_automatico = false;
                        // bumpless transfer: mantemos os valores atuais de atuadores (não alteramos aceleracao/direcao)
                        // Publicar os valores atuais via MQTT para garantir coerência externa e log
                        publicar_atuadores(DEFAULT_TRUCK_ID, aceleracao, direcao);

                        Atuadores atu_m;
                        atu_m.id = DEFAULT_TRUCK_ID;
                        atu_m.aceleracao = aceleracao;
                        atu_m.direcao = direcao;
                        try { buf.push(atu_m); } catch(...) {}

                        std::cout << "[LogicaDeComando] MODO MANUAL ativado (bumpless)\n";
                    } else {
                        std::cout << "[LogicaDeComando] SET_MANUAL ignorado: defeito presente\n";
                    }
                    break;

                case TipoComando::SET_AUTOMATICO:
                    if (!estado.e_defeito) {
                        estado.e_automatico = true;
                        std::cout << "[LogicaDeComando] MODO AUTOMÁTICO ativado\n";
                    } else {
                        std::cout << "[LogicaDeComando] SET_AUTOMATICO ignorado (defeito presente)\n";
                    }
                    break;

                case TipoComando::REARME_FALHA:
                    // Rearme: limpar defeito e permitir reativação automática manual
                    estado.e_defeito = false;
                    std::cout << "[LogicaDeComando] REARME de falha executado\n";
                    break;

                case TipoComando::ACELERA:
                    // Só no modo MANUAL e sem defeito
                    if (!estado.e_defeito && !estado.e_automatico) {
                        aceleracao += 10;
                        if (aceleracao > 100) aceleracao = 100;
                        // publicar via MQTT e também enviar ao buffer para log
                        publicar_atuadores(DEFAULT_TRUCK_ID, aceleracao, direcao);
                        Atuadores atu_a;
                        atu_a.id = DEFAULT_TRUCK_ID;
                        atu_a.aceleracao = aceleracao;
                        atu_a.direcao = direcao;
                        try { buf.push(atu_a); } catch(...) {}
                        std::cout << "[LogicaDeComando] MANUAL: ACELERA -> " << aceleracao << "\n";
                    } else {
                        std::cout << "[LogicaDeComando] COMANDO ACELERA ignorado (modo automático ou defeito)\n";
                    }
                    break;

                case TipoComando::GIRA_DIREITA:
                    if (!estado.e_defeito && !estado.e_automatico) {
                        direcao += 10;
                        if (direcao > 180) direcao = 180;
                        publicar_atuadores(DEFAULT_TRUCK_ID, aceleracao, direcao);
                        Atuadores atu_r;
                        atu_r.id = DEFAULT_TRUCK_ID;
                        atu_r.aceleracao = aceleracao;
                        atu_r.direcao = direcao;
                        try { buf.push(atu_r); } catch(...) {}
                        std::cout << "[LogicaDeComando] MANUAL: GIRA_DIREITA -> " << direcao << "\n";
                    } else {
                        std::cout << "[LogicaDeComando] COMANDO GIRA_DIREITA ignorado (modo automático ou defeito)\n";
                    }
                    break;

                case TipoComando::GIRA_ESQUERDA:
                    if (!estado.e_defeito && !estado.e_automatico) {
                        direcao -= 10;
                        if (direcao < -180) direcao = -180;
                        publicar_atuadores(DEFAULT_TRUCK_ID, aceleracao, direcao);
                        Atuadores atu_l;
                        atu_l.id = DEFAULT_TRUCK_ID;
                        atu_l.aceleracao = aceleracao;
                        atu_l.direcao = direcao;
                        try { buf.push(atu_l); } catch(...) {}
                        std::cout << "[LogicaDeComando] MANUAL: GIRA_ESQUERDA -> " << direcao << "\n";
                    } else {
                        std::cout << "[LogicaDeComando] COMANDO GIRA_ESQUERDA ignorado (modo automático ou defeito)\n";
                    }
                    break;

                default:
                    break;
            }
        }
        // 3) Estado debug
        std::cout << "[Estado] AUTO=" << estado.e_automatico
                  << " DEF=" << estado.e_defeito
                  << " ACC=" << aceleracao
                  << " DIR=" << direcao
                  << std::endl;

        // pequena espera para evitar loop excessivo
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    std::cout << "[LogicaDeComando] encerrando...\n";
}
