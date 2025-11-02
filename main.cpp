#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <numeric>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <classes.hpp>
#include <functional>
#include <variant>

// Função para simular leitura de dados do servidor (futuramente, substitua por publisher/subscriber real)
DadosProcessados lerDadosServidor() {
    // Simulação: gera valores aleatórios
    static int contador = 0;
    DadosProcessados dados;
    dados.id = ++contador; // ID único
    dados.posicao_x = 100 + contador;
    dados.posicao_y = 200 + contador;
    dados.angulo_x = 0 + contador;
    return dados;
}

// INÍCIO MONITORAMENTO DE FALHAS

// Função para simular a leitura dos sensores de falha (i_temperatura, i_falha_eletrica, etc.).
// No sistema real, esta função seria substituída por uma leitura de hardware ou um subscriber MQTT.
FalhaEvento lerSensoresDeFalha() {
    // Usa uma variável estática para manter o estado entre as chamadas, simulando a passagem do tempo.
    static int ciclo = 0;
    ciclo++; // Incrementa o contador de ciclo a cada chamada.

    // Simula um cenário de alerta de temperatura após 5 ciclos de execução.
    if (ciclo > 5 && ciclo <= 10) {
        // Retorna um objeto FalhaEvento com o tipo de falha e uma descrição.
        return {TipoFalha::TemperaturaAlerta, "Alerta: Temperatura do motor atingiu 98C."};
    }
    // Simula um cenário de falha elétrica crítica após 10 ciclos.
    if (ciclo > 10 && ciclo <= 15) {
        // Retorna um evento de falha elétrica.
        return {TipoFalha::Eletrica, "Falha Crítica: Sistema elétrico comprometido."};
    }
    // Após 15 ciclos, reinicia o contador para repetir o padrão de simulação de falhas.
    if (ciclo > 15) {
        ciclo = 0;
    }
    
    // Se nenhuma das condições de falha acima for atendida, retorna um estado OK.
    // Isso representa o funcionamento normal do caminhão.
    return {TipoFalha::OK, "Sistema operando normalmente."};
}

// FIM MONITORAMENTO DE FALHAS

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


// Função da thread para a tarefa de Monitoramento de Falhas.
// Ela é executada em paralelo com as outras tarefas do sistema.
void MonitoramentoDeFalhas(BufferCircular& buf) {
    // Cria um contexto de I/O do Boost.Asio para gerenciar eventos assíncronos, como o timer.
    boost::asio::io_context io;
    // Define um timer para executar a tarefa periodicamente a cada 500ms.
    // A verificação de falhas pode ser menos frequente que a leitura de sensores de posição.
    boost::asio::steady_timer timer(io, std::chrono::milliseconds(500)); 
    // Calcula o próximo ponto no tempo para a execução do timer.
    auto next_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);

    // Define a função (lambda) que será executada quando o timer disparar.
    std::function<void(const boost::system::error_code&)> handler = [&](const boost::system::error_code& ec) {
        // Chama a função de simulação para obter o estado atual dos sensores de falha.
        FalhaEvento evento = lerSensoresDeFalha();

        // Condição CRÍTICA: "Dispara o evento" (coloca no buffer) APENAS se uma falha real for detectada.
        // Isso evita poluir o buffer com mensagens de "OK", que não são eventos e desperdiçariam recursos.
        if (evento.tipo!= TipoFalha::OK) {
            // Insere o evento de falha no buffer circular compartilhado.
            // As outras tarefas (consumidores) poderão agora ler e reagir a este evento.
            buf.push(evento);
            // Imprime uma mensagem no console para fins de depuração, confirmando que a falha foi detectada e disparada.
            std::cout << "MonitoramentoDeFalhas: Evento disparado - " << evento.descricao << std::endl;
        }

        // Reagenda o timer para a próxima execução, garantindo o comportamento cíclico da tarefa.
        next_time += std::chrono::milliseconds(500);
        timer.expires_at(next_time);
        timer.async_wait(handler);
    };

    // Inicia a espera assíncrona pelo timer. A função 'handler' será chamada na primeira vez que o timer expirar.
    timer.async_wait(handler);
    // Executa o loop de eventos do io_context. Isso mantém a thread ativa, processando os eventos do timer.
    io.run();
}

// FIM MONITORAMENTO DE FALHAS

// INÍCIO LÓGICA DE COMANDO

// Função da thread para a tarefa de Lógica de Comando.
// Esta função atua como um "consumidor", lendo dados do buffer e tomando decisões.
void LogicaDeComando(BufferCircular& buf) {
    // Cria um objeto para guardar o estado atual do veículo.
    // Este objeto será atualizado conforme os dados são lidos do buffer.
    EstadoVeiculo estado_atual;

    // Inicia um loop infinito. A thread ficará aqui para sempre, processando dados.
    while (true) {
        // A linha mais importante: a thread vai tentar pegar um item do buffer.
        // Se o buffer estiver vazio, a função 'pop()' vai fazer a thread esperar (bloquear)
        // até que um novo item seja colocado por outra thread (TratamentoSensores ou MonitoramentoDeFalhas).
        DataVariant item = buf.pop();

        // 'std::visit' é uma ferramenta do C++ moderno para lidar com 'std::variant'.
        // Ele inspeciona o tipo de dado que está dentro de 'item' e executa o código correto para ele.
        // O trecho '[&](auto&& arg)' cria uma função anônima (lambda) que pode acessar as variáveis locais (como 'estado_atual').
        std::visit([&](auto&& arg) {
            // Descobre o tipo exato do dado que foi retirado do buffer.
            using T = std::decay_t<decltype(arg)>;

            // Se o tipo do dado for 'DadosProcessados' (vindo de TratamentoSensores)...
            if constexpr (std::is_same_v<T, DadosProcessados>) {
                // Por enquanto, apenas imprimimos uma mensagem para confirmar que recebemos os dados.
                // No futuro, aqui entraria a lógica para enviar comandos aos atuadores (o_aceleracao, o_direcao).
                // 'arg' aqui é o objeto 'DadosProcessados' que foi lido.
                std::cout << "LogicaDeComando: Recebeu dados de sensor. ID: " << arg.id << std::endl;
            
            // Se o tipo do dado for 'FalhaEvento' (vindo de MonitoramentoDeFalhas)...
            } else if constexpr (std::is_same_v<T, FalhaEvento>) {
                // Imprime a descrição da falha que foi recebida.
                std::cout << "LogicaDeComando: Recebeu evento de FALHA! Desc: " << arg.descricao << std::endl;
                // Atualiza o estado do veículo para indicar que há um defeito ativo.
                // Isso corresponde a 'e_defeito = 1'.
                estado_atual.e_defeito = true;

            // Se o tipo do dado for 'ComandoOperador' (viria da Interface Local)...
            } else if constexpr (std::is_same_v<T, ComandoOperador>) {
                // Imprime o tipo de comando recebido para depuração.
                std::cout << "LogicaDeComando: Recebeu um COMANDO do operador." << std::endl;
                
                // Usa uma estrutura 'switch' para executar uma ação diferente para cada tipo de comando.
                switch (arg.comando) {
                    // Caso o comando seja para ativar o modo automático...
                    case TipoComando::SET_AUTOMATICO:
                        // Atualiza o estado para modo automático. Corresponde a 'e_automatico = 1'.
                        estado_atual.e_automatico = true;
                        break; // Termina o processamento deste caso.
                    // Caso o comando seja para ativar o modo manual...
                    case TipoComando::SET_MANUAL:
                        // Atualiza o estado para modo manual. Corresponde a 'e_automatico = 0'.
                        estado_atual.e_automatico = false;
                        break;
                    // Caso o comando seja para rearmar uma falha...
                    case TipoComando::REARME_FALHA:
                        // Atualiza o estado para indicar que não há mais defeito. Corresponde a 'e_defeito = 0'.
                        estado_atual.e_defeito = false;
                        break;
                    // Outros comandos (acelerar, etc.) seriam tratados aqui no futuro.
                    default:
                        break;
                }
            }
        }, item); // Fim do std::visit

        // Após processar um item e potencialmente atualizar o estado, imprime o estado atual do veículo.
        // Isso nos permite ver em tempo real como a Lógica de Comando está funcionando.
        // A expressão '(estado_atual.e_automatico? "Automatico" : "Manual")' é um atalho para escolher a string correta.
        std::cout << ">> ESTADO ATUAL DO VEICULO:[Modo: " << (estado_atual.e_automatico? "Automatico" : "Manual") << "]" << std::endl;
    }
}

// FIM LÓGICA DE COMANDO

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