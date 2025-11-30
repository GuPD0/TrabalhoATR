#include "ColetorDeDados.hpp"
#include "classes.hpp"
#include "MonitoramentoDeFalhas.hpp"
#include "mqtt.hpp"
#include "ipc_shared.hpp"

#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstring>

// sockets
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

// flags de controle
extern std::atomic<bool> running;

// flags globais de falha (já existentes no sistema)
extern std::map<int, FalhasStatus> falhas_caminhoes;
extern std::mutex falhas_mutex;

// Estado atual para envio via IPC (protegido por mutex)
static EstadoDisplay estado_global;
static std::mutex mtx_estado;

// THREAD SERVIDORA IPC (Roda dentro do Coletor)
void ServidorIPC(BufferCircular& buf) {
    int server_fd = -1;
    int new_socket = -1;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    // 1. Criar Socket TCP
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[IPC] Falha ao criar socket");
        return;
    }

    // Permitir reutilização da porta
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("[IPC] setsockopt(SO_REUSEADDR) falhou");
        close(server_fd);
        return;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORTA_IPC);

    // 2. Bind
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("[IPC] Falha no bind");
        close(server_fd);
        return;
    }

    // 3. Listen (1 cliente - Interface Local)
    if (listen(server_fd, 1) < 0) {
        perror("[IPC] Falha no listen");
        close(server_fd);
        return;
    }

    std::cout << "[Coletor][IPC] Aguardando Interface Local na porta " << PORTA_IPC << "...\n";

    // loop principal do servidor (aceita reconexões)
    while (running.load()) {
        fd_set readfds;
        struct timeval tv;

        // Aceita conexão (bloqueante por design — mas só um cliente; isso fica numa thread)
        new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket < 0) {
            if (!running.load()) break;
            perror("[IPC] accept falhou");
            // tentar novamente
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        std::cout << "[Coletor][IPC] Interface Local conectada (" 
                  << inet_ntoa(address.sin_addr) << ":" << ntohs(address.sin_port) << ")\n";

        // Comunicação com o cliente conectado
        while (running.load()) {
            // 1) Enviar snapshot do estado
            {
                std::lock_guard<std::mutex> lock(mtx_estado);
                ssize_t sent = send(new_socket, &estado_global, sizeof(estado_global), 0);
                if (sent < 0) {
                    perror("[IPC] send falhou");
                    break;
                }
            }

            // 2) Usar select para checar se há dados a serem lidos (timeout curto)
            FD_ZERO(&readfds);
            FD_SET(new_socket, &readfds);
            tv.tv_sec = 0;
            tv.tv_usec = 100000; // 100 ms

            int activity = select(new_socket + 1, &readfds, nullptr, nullptr, &tv);
            if (activity < 0) {
                perror("[IPC] select falhou");
                break;
            }
            if (activity > 0 && FD_ISSET(new_socket, &readfds)) {
                // tenta ler um comando
                ComandoPacote pacote_cmd;
                ssize_t valread = recv(new_socket, &pacote_cmd, sizeof(pacote_cmd), 0);
                if (valread <= 0) {
                    // cliente desconectou
                    if (valread == 0) {
                        std::cout << "[Coletor][IPC] Interface desconectou (client closed)\n";
                    } else {
                        perror("[IPC] recv falhou");
                    }
                    break;
                }
                // traduzir comando e publicar no buffer
                ComandoOperador cmd_op;
                bool valido = true;
                switch (pacote_cmd.comando) {
                    case CmdType::CMD_AUTO:    cmd_op.comando = TipoComando::SET_AUTOMATICO; break;
                    case CmdType::CMD_MANUAL:  cmd_op.comando = TipoComando::SET_MANUAL;     break;
                    case CmdType::CMD_REARME:  cmd_op.comando = TipoComando::REARME_FALHA;  break;
                    case CmdType::CMD_ACELERA: cmd_op.comando = TipoComando::ACELERA;       break;
                    case CmdType::CMD_ESQUERDA:cmd_op.comando = TipoComando::GIRA_ESQUERDA; break;
                    case CmdType::CMD_DIREITA: cmd_op.comando = TipoComando::GIRA_DIREITA;  break;
                    default: valido = false; break;
                }
                if (valido) {
                    try {
                        buf.push(cmd_op);
                        std::cout << "[Coletor][IPC] Comando recebido e enviado ao buffer: " 
                                  << static_cast<int>(pacote_cmd.comando) << "\n";
                    } catch (...) {
                        std::cerr << "[Coletor][IPC] Erro ao publicar comando no buffer\n";
                    }
                }
            }

            // pequeno sleep para não saturar CPU/linha
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // fechar socket do cliente e voltar para aceitar
        close(new_socket);
        new_socket = -1;
        std::cout << "[Coletor][IPC] Cliente desconectado. Voltando a aceitar conexões...\n";
    }

    // fecha socket servidor
    if (server_fd >= 0) close(server_fd);
    std::cout << "[Coletor][IPC] Servidor IPC encerrado.\n";
}
//  COLETOR DE DADOS — COMPORTAMENTO REAL
//  Lê todos os eventos do buffer + estados de falha.
//  Gera um log em arquivo com timestamp.
//  Futuramente: envia estados via IPC para Interface Local.
//
void ColetorDeDados(BufferCircular& buf)
{
    // Inicia a thread servidora IPC em paralelo
    std::thread thread_ipc(ServidorIPC, std::ref(buf));
    thread_ipc.detach(); // roda em background
    
    std::ofstream logfile("log_caminhao.txt", std::ios::app);
    if (!logfile.is_open()) {
        std::cerr << "[Coletor] ERRO: não foi possível abrir arquivo de log!" << std::endl;
        return;
    }
    std::cout << "[Coletor] Thread de coleta iniciada, registrando em log_caminhao.txt\n";

    while (running.load()) {
        // 1. LER EVENTOS DO BUFFER
        DataVariant item = buf.pop();

        auto now = std::chrono::system_clock::now();
        std::time_t ts = std::chrono::system_clock::to_time_t(now);

        logfile << "[" << ts << "] ";

        // SENSORES 
        if (std::holds_alternative<DadosSensores>(item)) {
            auto s = std::get<DadosSensores>(item);
            // Atualiza estado_global para IPC
            {
                std::lock_guard<std::mutex> lock(mtx_estado);
                estado_global.pos_x = s.pos_x;
                estado_global.pos_y = s.pos_y;
                estado_global.angulo = s.angulo;
                estado_global.temperatura = s.temperatura;
                estado_global.falha_eletrica = s.falha_eletrica;
                estado_global.falha_hidraulica = s.falha_hidraulica;
                // modo_automatico / com_defeito devem ser atualizados por eventos da LogicaDeComando
                // (caso não haja essa ligação ainda, o estado permanecerá como o último valor)
            }
            
            logfile << "SENSORES | "
                    << "X=" << s.pos_x
                    << " Y=" << s.pos_y
                    << " ANG=" << s.angulo
                    << " TEMP=" << s.temperatura
                    << " FE=" << s.falha_eletrica
                    << " FH=" << s.falha_hidraulica
                    << "\n";
        }

        // FALHAS 
        else if (std::holds_alternative<FalhaEvento>(item)) {
            auto f = std::get<FalhaEvento>(item);

            logfile << "FALHA | " << f.descricao << "\n";
        }

        //  PROCESSADOS
        else if (std::holds_alternative<DadosProcessados>(item)) {
            auto p = std::get<DadosProcessados>(item);

            // Atualiza, se quiser, algum elemento do estado_global (ex: posição estimada)
            {
                std::lock_guard<std::mutex> lock(mtx_estado);
                estado_global.pos_x = p.posicao_x;
                estado_global.pos_y = p.posicao_y;
                estado_global.angulo = p.angulo_x;
            }

            logfile << "PROCESSADO | ID=" << p.id
                    << " X=" << p.posicao_x
                    << " Y=" << p.posicao_y
                    << " ANG=" << p.angulo_x
                    << "\n";
        }

        // COMANDOS
        else if (std::holds_alternative<ComandoOperador>(item)) {
            auto c = std::get<ComandoOperador>(item);

            logfile << "COMANDO | Tipo=" << (int)c.comando << "\n";
        }

        logfile.flush();

        // 2. VERIFICAR FLAGS DE FALHA DAS OUTRAS THREADS (via eventos)
        {
            std::lock_guard<std::mutex> lock(falhas_mutex);

            for (auto& [id, status] : falhas_caminhoes) {

                if (status.temperatura.exchange(false)) {
                    logfile << "[" << ts << "] EVENTO | Caminhao " << id
                            << " | Falha de Temperatura\n";
                }

                if (status.eletrica.exchange(false)) {
                    logfile << "[" << ts << "] EVENTO | Caminhao " << id
                            << " | Falha Elétrica\n";
                }

                if (status.hidraulica.exchange(false)) {
                    logfile << "[" << ts << "] EVENTO | Caminhao " << id
                            << " | Falha Hidráulica\n";
                }
            }
        }
        // pequena espera para suavizar loops (já que pop é bloqueante normalmente)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    logfile.close();
    std::cout << "[Coletor] Encerrando coleta e logs.\n";
}