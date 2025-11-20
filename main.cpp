#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <numeric>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include "classes.hpp"
#include <functional>
#include <variant>
#include "SimulacaoDaMina.cpp"
#include "threads.cpp"

int main() {
    BufferCircular buf;

    // INÍCIO LÓGICA DE COMANDO
    // Para testar a lógica de comandos sem ter a Interface Local pronta,
    // podemos colocar um comando de teste diretamente no buffer antes de iniciar as threads.
    // Aqui, estamos simulando o operador pressionando a tecla para o MODO MANUAL.
    buf.push(ComandoOperador{TipoComando::SET_MANUAL});
    // FIM LÓGICA DE COMANDO

    // Inicia a thread TratamentoSensores
    std::thread thread_sensores(TratamentoSensores, std::ref(buf));

    // INÍCIO MONITORAMENTO DE FALHAS
    // Cria e inicia a thread para a tarefa de Monitoramento de Falhas.
    // A função MonitoramentoDeFalhas será executada em seu próprio fluxo de controle.
    // std::ref(buf) é usado para passar o buffer por referência para a thread.
    std::thread thread_falhas(MonitoramentoDeFalhas, std::ref(buf));
    // FIM MONITORAMENTO DE FALHAS

    // INÍCIO LÓGICA DE COMANDO
    // Cria e inicia a thread para a tarefa de Lógica de Comando.
    // A função LogicaDeComando será executada em seu próprio fluxo de controle.
    std::thread thread_logica(LogicaDeComando, std::ref(buf));
    // FIM LÓGICA DE COMANDO

    // Mantém a thread principal viva por 10 segundos para observar a execução das outras threads.
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // Desacopla as threads da thread principal.
    // Isso permite que a função main termine sem precisar esperar (join) pelas threads.
    // ATENÇÃO: Em uma aplicação final, um mecanismo de encerramento limpo (com join) é preferível.
    thread_sensores.detach();
    thread_falhas.detach();

    std::cout << "Execução principal concluída após 10 segundos." << std::endl;
    return 0;
}