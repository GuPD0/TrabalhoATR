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
#include <atomic>
#include <signal.h>
#include "threads.hpp"
#include "mqtt.hpp"
#include "controle_navegacao.hpp"
#include "ColetorDeDados.hpp"

std::atomic<bool> running{true};

// Função para capturar Ctrl+C
void handle_sigint(int) {
    running.store(false);
}

int main() {
    signal(SIGINT, handle_sigint);
    BufferCircular buf;
    iniciar_mqtt(buf);
    
    // thread dedicada para o loop do MQTT
    std::thread thread_mqtt([](){
        while (true) {
            mqtt_loop();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    // Inicia a thread ControleDeNavegacao
    std::thread thread_controle(ControleDeNavegacao, std::ref(buf));

    // INÍCIO LÓGICA DE COMANDO
    // Para testar a lógica de comandos sem ter a Interface Local pronta,
    // podemos colocar um comando de teste diretamente no buffer antes de iniciar as threads.
    // Aqui, estamos simulando o operador pressionando a tecla para o MODO MANUAL.
    //buf.push(ComandoOperador{TipoComando::SET_MANUAL});
    // função ainda não implementada
    // FIM LÓGICA DE COMANDO

    // Inicia a thread TratamentoSensores
    std::thread thread_sensores(TratamentoSensores, std::ref(buf));

    // Inicia a thread ColetorDeDados
    std::thread thread_coletor(ColetorDeDados, std::ref(buf));

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
    while (running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // Desacopla as threads da thread principal.
    // Isso permite que a função main termine sem precisar esperar (join) pelas threads.
    // ATENÇÃO: Em uma aplicação final, um mecanismo de encerramento limpo (com join) é preferível.
    // thread_sensores.detach();
    // thread_falhas.detach();
    // thread_logica.detach();

    // Sinaliza para as threads pararem
    running.store(false);

    // Espera todas as threads terminarem
    thread_mqtt.join();
    thread_sensores.join();
    thread_falhas.join();
    thread_logica.join();
    thread_controle.join();
    thread_coletor.join();

    std::cout << "Execução principal concluída após 10 segundos." << std::endl;
    return 0;
}