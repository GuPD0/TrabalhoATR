#include "TratamentoSensores.hpp"
#include "classes.hpp"
#include "planejamento.hpp"

#include <iostream>
#include <cmath>

extern std::atomic<bool> running;

// ======================================================================
// FUNÇÃO AUXILIAR: Média angular correta (para evitar erro de wrap-around)
// ======================================================================
static double media_angular(const std::vector<int>& amostras)
{
    if (amostras.empty()) return 0.0;

    double sum_sin = 0.0;
    double sum_cos = 0.0;

    for (int ang : amostras) {
        double rad = ang * M_PI / 180.0;
        sum_cos += cos(rad);
        sum_sin += sin(rad);
    }

    return atan2(sum_sin, sum_cos) * 180.0 / M_PI;
}

// ======================================================================
// THREAD: Tratamento de Sensores (PDF — filtro + envio ao buffer)
// ======================================================================
void TratamentoSensores(BufferCircular& buf)
{
    // Filtros de posição e ângulo
    FiltroMediaMovel filtro_x(5);
    FiltroMediaMovel filtro_y(5);
    std::vector<int> historico_ang;     // para média angular
    const size_t ordem_ang = 5;

    while (running.load()) {

        // 1) Recebe do buffer principal
        DataVariant item = buf.pop();

        if (!std::holds_alternative<DadosSensores>(item)) {
            // ignora itens que não são sensores
            continue;
        }

        DadosSensores s = std::get<DadosSensores>(item);
        // logo após obter `DadosSensores s = std::get<DadosSensores>(item);`

        // --- LOG RAW (útil pra debug) ---
        std::cout << "[TratamentoSensores][RAW] pos=(" << s.pos_x << "," << s.pos_y
                << ") ang=" << s.angulo << " temp=" << s.temperatura
                << " fe=" << s.falha_eletrica << " fh=" << s.falha_hidraulica << std::endl;

        // --- VALIDAÇÃO / CLAMP (protege contra valores absurdos) ---
        const int POS_LIMIT = 100000;   // ajuste conforme seu domínio (ex.: 5k já usei antes)
        const int TEMP_MIN = -200;
        const int TEMP_MAX = 500;

        if (s.pos_x < -POS_LIMIT || s.pos_x > POS_LIMIT ||
            s.pos_y < -POS_LIMIT || s.pos_y > POS_LIMIT ||
            s.angulo < -10000 || s.angulo > 10000 ||
            s.temperatura < TEMP_MIN || s.temperatura > TEMP_MAX)
        {
            std::cerr << "[TratamentoSensores][WARN] valor fora do esperado, descartando amostra: "
                    << "X="<<s.pos_x<<" Y="<<s.pos_y<<" ANG="<<s.angulo<<" TEMP="<<s.temperatura<<"\n";
            continue; // descarta amostra corrupta
        }

        // Agora aplique clamp pequeno (opcional) para manter valores sensatos no filtro
        s.pos_x = std::max(-POS_LIMIT, std::min(POS_LIMIT, s.pos_x));
        s.pos_y = std::max(-POS_LIMIT, std::min(POS_LIMIT, s.pos_y));
        s.angulo = ((s.angulo % 360) + 360) % 360; // normaliza 0..359 se fizer sentido no seu sistema

        // 2) Filtragem de posição
        int x_f = filtro_x.filtrar(s.pos_x);
        int y_f = filtro_y.filtrar(s.pos_y);

        // 3) Filtragem de ângulo com média vetorial (CORRETO)
        historico_ang.push_back(s.angulo);
        if (historico_ang.size() > ordem_ang)
            historico_ang.erase(historico_ang.begin());

        double ang_f = media_angular(historico_ang);
        
        // --- atualizar posição atual para Planejamento de Rota ---
        ultima_posicao_conhecida.x.store(x_f);
        ultima_posicao_conhecida.y.store(y_f);
        ultima_posicao_conhecida.ang.store(static_cast<int>(std::round(ang_f)));

        // 4) Debug (opcional)
        std::cout << "[TratamentoSensores] "
                  << "X=" << x_f
                  << " Y=" << y_f
                  << " ANG=" << ang_f
                  << " TEMP=" << s.temperatura
                  << " FE=" << s.falha_eletrica
                  << " FH=" << s.falha_hidraulica
                  << std::endl;

        // 5) Criar DadosProcessados para outras tarefas
        DadosProcessados dp;
        dp.id = 1; // futuramente substituir pelo ID real do caminhão
        dp.posicao_x = x_f;
        dp.posicao_y = y_f;
        dp.angulo_x = static_cast<int>(std::round(ang_f));

        buf.push(dp);
    }

    std::cout << "[TratamentoSensores] encerrando..." << std::endl;
}
