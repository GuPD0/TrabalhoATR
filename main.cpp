#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <numeric>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <functional>
#include <variant>
#include <atomic>
#include <signal.h>

#include "classes.hpp"
#include "mqtt.hpp"
#include "controle_navegacao.hpp"
#include "ColetorDeDados.hpp"
#include "TratamentoSensores.hpp"
#include "planejamento.hpp"
#include "LogicaDeComando.hpp"
#include "MonitoramentoDeFalhas.hpp"

//flag global de execução
std::atomic<bool> running{true};

// Função para capturar Ctrl+C
void handle_sigint(int) {
    running.store(false);
}

int main() {
    signal(SIGINT, handle_sigint);
    BufferCircular buf;
    //Inicia o MQTT
    iniciar_mqtt(buf);
    
    // thread dedicada para o loop do MQTT
    std::thread thread_mqtt([](){
        while (running.load()) {
            mqtt_loop();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // Inicia a thread TratamentoSensores
    std::thread thread_sensores(TratamentoSensores, std::ref(buf));

    // Inicia a thread LogicaDeComando
    std::thread thread_logica(LogicaDeComando, std::ref(buf));

    // Inicia a thread ControleDeNavegacao
    std::thread thread_controle(ControleDeNavegacao, std::ref(buf));

    // Inicia a thread PlanejamentoDeRota
    std::thread thread_planejamento(PlanejamentoDeRota, std::ref(buf));

    // Inicia a thread ColetorDeDados
    std::thread thread_coletor(ColetorDeDados, std::ref(buf));

    // Inicio a thread Monitoramento de Falhas
    std::thread thread_falhas(MonitoramentoDeFalhas);

    // Loop main aguardando finalização
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "[MAIN] Encerrando todas as threads..." << std::endl;
    running.store(false);

    // Finalizar threads
    thread_mqtt.join();
    thread_sensores.join();
    thread_logica.join();
    thread_controle.join();
    thread_planejamento.join();
    thread_coletor.join();
    thread_falhas.join();

    std::cout << "Execução principal encerrada." << std::endl;
    return 0;
}