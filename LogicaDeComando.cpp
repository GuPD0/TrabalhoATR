#include "LogicaDeComando.hpp"
#include "mqtt.hpp"        // publicar_atuadores
#include "threads.hpp"     // declarações de flags e estruturas (se necessário)
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

void LogicaDeComando(BufferCircular& buf)
{
    EstadoVeiculo estado;
    int aceleracao = 0; // -100..100
    int direcao = 0;    // -180..180

    // Para evitar repassar evento múltiplas vezes, usamos variáveis locais
    while (running.load()) {

        // 0) Checar eventos de falha vindos por flags/atomics (monitoramento de falhas)
        // Se alguma flag estiver true, cria-se um FalhaEvento e empurra no buffer
        // (assim ColetorDeDados e Controle recebem o evento)
        if (falha_temperatura.exchange(false)) {
            FalhaEvento fe;
            fe.tipo = TipoFalha::TemperaturaCritica;
            fe.descricao = "Temperatura crítica detectada (flag).";
            try { buf.push(fe); } catch (...) {}
            estado.e_defeito = true;
            std::cout << "[LogicaDeComando] Evento: Temperatura crítica (flag)\n";
        }
        if (falha_eletrica.exchange(false)) {
            FalhaEvento fe;
            fe.tipo = TipoFalha::Eletrica;
            fe.descricao = "Falha elétrica detectada (flag).";
            try { buf.push(fe); } catch (...) {}
            estado.e_defeito = true;
            std::cout << "[LogicaDeComando] Evento: Falha elétrica (flag)\n";
        }
        if (falha_hidraulica.exchange(false)) {
            FalhaEvento fe;
            fe.tipo = TipoFalha::Hidraulica;
            fe.descricao = "Falha hidráulica detectada (flag).";
            try { buf.push(fe); } catch (...) {}
            estado.e_defeito = true;
            std::cout << "[LogicaDeComando] Evento: Falha hidráulica (flag)\n";
        }

        // 1) Ler próximo item do buffer (bloqueante)
        DataVariant item = buf.pop();

        // 2) PROCESSAR tipos recebidos
        // 2.a DadosProcessados -> atualização de estado / decisão automática
        if (std::holds_alternative<DadosProcessados>(item)) {
            auto dp = std::get<DadosProcessados>(item);

            // Se estiver no modo automático e não houver defeito, definir comandos automáticos simples
            if (estado.e_automatico && !estado.e_defeito) {
                // Lógica simples: aceleração fixa leve, direção neutra.
                // (Você pode trocar por controle mais sofisticado / setpoints vindos do Planejamento)
                aceleracao = 20; // valor por default
                direcao = 0;

                // publicar via MQTT e também empurrar Atuadores no buffer
                publicar_atuadores(DEFAULT_TRUCK_ID, aceleracao, direcao);

                Atuadores atu;
                atu.id = DEFAULT_TRUCK_ID;
                atu.aceleracao = aceleracao;
                atu.direcao = direcao;
                try { buf.push(atu); } catch (...) {}

                std::cout << "[LogicaDeComando] AUTO: publicados atuadores (acc=" << aceleracao << " dir=" << direcao << ")\n";
            }
        }

        // 2.b Evento de Falha (proveniente de Monitoramento de Falhas que escreveu no buffer)
        else if (std::holds_alternative<FalhaEvento>(item)) {
            auto falha = std::get<FalhaEvento>(item);
            // Se falha grave, travar o veículo
            if (falha.tipo == TipoFalha::TemperaturaCritica ||
                falha.tipo == TipoFalha::Eletrica ||
                falha.tipo == TipoFalha::Hidraulica) 
            {
                estado.e_defeito = true;
                aceleracao = 0;
                direcao = 0;
                publicar_atuadores(DEFAULT_TRUCK_ID, aceleracao, direcao);

                Atuadores atu;
                atu.id = DEFAULT_TRUCK_ID;
                atu.aceleracao = aceleracao;
                atu.direcao = direcao;
                try { buf.push(atu); } catch (...) {}

                std::cout << "[LogicaDeComando] FALHA GRAVE PROCESSADA -> atuadores zerados\n";
            } else if (falha.tipo == TipoFalha::TemperaturaAlerta) {
                std::cout << "[LogicaDeComando] ALERTA de temperatura (evento)\n";
            }
        }

        // 2.c Comando do operador (Interface Local)
        else if (std::holds_alternative<ComandoOperador>(item)) {
            auto cmd = std::get<ComandoOperador>(item);
            switch (cmd.comando) {
                case TipoComando::SET_MANUAL:
                    if (!estado.e_defeito) {
                        estado.e_automatico = false;
                        // bumpless: deixar atuadores com valores atuais (não alterar)
                        std::cout << "[LogicaDeComando] MODO MANUAL ativado (bumpless)\n";
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
                    if (!estado.e_defeito && !estado.e_automatico) {
                        aceleracao += 10;
                        if (aceleracao > 100) aceleracao = 100;
                        std::cout << "[LogicaDeComando] MANUAL: ACELERA -> " << aceleracao << "\n";
                    }
                    break;

                case TipoComando::GIRA_DIREITA:
                    if (!estado.e_defeito && !estado.e_automatico) {
                        direcao += 10;
                        if (direcao > 180) direcao = 180;
                        std::cout << "[LogicaDeComando] MANUAL: GIRA_DIREITA -> " << direcao << "\n";
                    }
                    break;

                case TipoComando::GIRA_ESQUERDA:
                    if (!estado.e_defeito && !estado.e_automatico) {
                        direcao -= 10;
                        if (direcao < -180) direcao = -180;
                        std::cout << "[LogicaDeComando] MANUAL: GIRA_ESQUERDA -> " << direcao << "\n";
                    }
                    break;

                default:
                    break;
            }

            // Após processar comando manual, publicar atuações atuais (manter coerência)
            publicar_atuadores(DEFAULT_TRUCK_ID, aceleracao, direcao);
            Atuadores atu;
            atu.id = DEFAULT_TRUCK_ID;
            atu.aceleracao = aceleracao;
            atu.direcao = direcao;
            try { buf.push(atu); } catch (...) {}
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
