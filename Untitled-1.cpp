// main.cpp (modificado para incluir a thread TratamentoSensores)
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <numeric> // Para std::accumulate
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include "BufferCircular.hpp"

// Função para simular leitura de dados do servidor (futuramente, substitua por publisher/subscriber real)
DadosProcessados lerDadosServidor() {
    // Simulação: gera valores aleatórios (substitua por leitura real)
    static int contador = 0;
    DadosProcessados dados;
    dados.posicao_x = 100 + contador; // Simula posição x
    dados.posicao_y = 200 + contador; // Simula posição y
    dados.angulo_x = 0 + contador;    // Simula ângulo
    contador++;
    return dados;
}

// Thread TratamentoSensores
void TratamentoSensores(BufferCircular& buf) {
    boost::asio::io_context io;
    boost::asio::steady_timer timer(io, std::chrono::milliseconds(100)); // Ciclo a cada 100ms
    auto next_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);

    // Filtros para cada variável (ordem M=5)
    FiltroMediaMovel filtro_x(5);
    FiltroMediaMovel filtro_y(5);
    FiltroMediaMovel filtro_angulo(5);

    std::function<void(const boost::system::error_code&)> handler = [&](const boost::system::error_code& ec) {
        // Lê dados do servidor
        DadosProcessados dados_brutos = lerDadosServidor();

        // Aplica filtro de média móvel
        DadosProcessados dados_filtrados;
        dados_filtrados.posicao_x = filtro_x.filtrar(dados_brutos.posicao_x);
        dados_filtrados.posicao_y = filtro_y.filtrar(dados_brutos.posicao_y);
        dados_filtrados.angulo_x = filtro_angulo.filtrar(dados_brutos.angulo_x);

        // Armazena no buffer compartilhado
        buf.push(dados_filtrados);

        // Imprime para debug (opcional)
        std::cout << "TratamentoSensores: Dados filtrados - X: " << dados_filtrados.posicao_x
                  << ", Y: " << dados_filtrados.posicao_y << ", Angulo: " << dados_filtrados.angulo_x << std::endl;

        // Reagenda o timer
        next_time += std::chrono::milliseconds(100);
        timer.expires_at(next_time);
        timer.async_wait(handler);
    };

    timer.async_wait(handler);
    io.run(); // Mantém a thread rodando
}

int main() {
    BufferCircular buf(200); // Buffer de 200 posições

    // Thread TratamentoSensores
    std::thread thread_sensores(TratamentoSensores, std::ref(buf));

    // Para a thread sensores após um tempo (exemplo: 5 segundos)
    std::this_thread::sleep_for(std::chrono::seconds(5));
    // Nota: Futuramente será necessário usar flag

    thread_sensores.join();

    std::cout << "Execução concluída." << std::endl;
    return 0;
}
