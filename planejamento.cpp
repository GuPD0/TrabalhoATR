#include "planejamento.hpp"
#include <cmath>
#include <iostream>
#include <thread>
#include <chrono>

// --- PLANEJAMENTO DE ROTA ---

// Definição das variáveis globais compartilhadas
Missao missao_atual;
std::mutex mtx_missao;
PosicaoAtual ultima_posicao_conhecida;

void PlanejamentoDeRota(BufferCircular& buf) {
    std::cout << "[Planejamento] Thread iniciada." << std::endl;

    while (true) { 
        Missao missao_local;
        
        // 1. Cópia segura da missão atual (protegida por mutex)
        {
            std::lock_guard<std::mutex> lock(mtx_missao);
            missao_local = missao_atual;
        }

        if (missao_local.ativa) {
            // 2. Leitura atômica da posição atual (sem bloqueio)
            int x_atual = ultima_posicao_conhecida.x.load();
            int y_atual = ultima_posicao_conhecida.y.load();
            int ang_atual = ultima_posicao_conhecida.ang.load();

            // 3. Matemática de Navegação
            double dx = missao_local.x_final - x_atual;
            double dy = missao_local.y_final - y_atual;
            double distancia = std::hypot(dx, dy);

            int vel_cmd = 0;
            int ang_cmd = 0;

            // Se chegou perto o suficiente (raio de 10 unidades), para.
            if (distancia < 10.0) {
                std::cout << "[Planejamento] DESTINO ALCANÇADO! X=" << x_atual << " Y=" << y_atual << std::endl;
                
                // Desativa a missão
                std::lock_guard<std::mutex> lock(mtx_missao);
                missao_atual.ativa = false;
                
                vel_cmd = 0;
                ang_cmd = ang_atual; // Mantém ângulo atual
            } else {
                // Lógica simples de aproximação
                vel_cmd = (distancia > 50.0) ? 60 : 30; // Reduz velocidade se estiver perto

                // Calcula ângulo desejado em graus
                double ang_rad = std::atan2(dy, dx);
                ang_cmd = static_cast<int>(ang_rad * 180.0 / M_PI);
                
                // Debug periódico
                // std::cout << "[Planejamento] Indo para " << missao_local.x_final << "," << missao_local.y_final 
                //           << " Dist=" << distancia << " AngAlvo=" << ang_cmd << std::endl;
            }

            // 4. Enviar Setpoint para o Buffer Circular
            // ID 999 indica que é um comando interno do Planejador para o Controlador
            DadosProcessados setpoint;
            setpoint.id = 999; 
            setpoint.posicao_x = 0;        // Não usado
            setpoint.posicao_y = vel_cmd;  // HACK: Usando Y para mandar a VELOCIDADE DESEJADA
            setpoint.angulo_x = ang_cmd;   // ÂNGULO DESEJADO

            buf.push(setpoint);
        }

        // Executa a 10Hz (a cada 100ms) para não sobrecarregar a CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}