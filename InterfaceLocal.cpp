#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <iomanip>

// Sockets (Linux)
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/select.h>

#include "ipc_shared.hpp"

std::atomic<bool> running{true};

// --------------------------------------------------------------
// Configuração do terminal para modo RAW (captura instantânea)
// --------------------------------------------------------------
void configurar_terminal(bool raw) {
    static struct termios oldt, newt;

    if (raw) {
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;

        newt.c_lflag &= ~(ICANON | ECHO);  // sem buffer de linha, sem eco
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }
}

// --------------------------------------------------------------
// Função estilo "kbhit" para saber se tem tecla pressionada
// --------------------------------------------------------------
int kbhit() {
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(1, &fds, NULL, NULL, &tv) > 0;
}

// --------------------------------------------------------------
// Programa Principal (Cliente IPC)
// --------------------------------------------------------------
int main() {

    std::cout << "Iniciando Interface Local...\n";

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "[InterfaceLocal] Erro ao criar socket.\n";
        return -1;
    }

    struct sockaddr_in serv_addr {};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORTA_IPC);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        std::cerr << "[InterfaceLocal] Endereço inválido.\n";
        return -1;
    }

    // Tentativa de conexão (loop retry)
    while (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cout << "[InterfaceLocal] Tentando conectar ao Coletor...\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "[InterfaceLocal] Conectado ao Coletor!\n";

    configurar_terminal(true);

    // Thread que RECEBE dados do caminhão
    std::thread receiver([&]() {

        EstadoDisplay estado;

        while (running.load()) {

            int valread = read(sock, &estado, sizeof(estado));
            if (valread <= 0) {
                std::cout << "[InterfaceLocal] Conexão perdida.\n";
                running = false;
                break;
            }

            // LIMPAR TELA
            std::cout << "\033[2J\033[H";

            std::cout << "======== INTERFACE DO OPERADOR ========\n";
            std::cout << "Posição X:     " << estado.pos_x << "\n";
            std::cout << "Posição Y:     " << estado.pos_y << "\n";
            std::cout << "Ângulo:        " << estado.angulo << " graus\n";
            std::cout << "Temperatura:   " << estado.temperatura << " C\n\n";

            std::cout << "Falhas: "
                      << (estado.falha_eletrica ? "[ELETRICA] " : "")
                      << (estado.falha_hidraulica ? "[HIDRAULICA] " : "")
                      << (!estado.falha_eletrica && !estado.falha_hidraulica ? "[OK]" : "")
                      << "\n\n";

            std::cout << "Modo atual: "
                      << (estado.modo_automatico ? "Automático" : "Manual")
                      << (estado.com_defeito ? " (DEFEITO!)" : "")
                      << "\n\n";

            std::cout << "=== COMANDOS ===\n";
            std::cout << "[1] Automático\n";
            std::cout << "[2] Manual\n";
            std::cout << "[R] Rearme falhas\n";
            std::cout << "[W] Acelera\n";
            std::cout << "[A] Esquerda\n";
            std::cout << "[D] Direita\n";
            std::cout << "[Q] Sair\n";
        }
    });

    // Thread principal captura teclado
    while (running.load()) {

        if (kbhit()) {
            char c = getchar();
            ComandoPacote cmd;
            cmd.comando = CmdType::NENHUM;

            if (c == 'q' || c == 'Q') { running = false; break; }
            else if (c == '1')         cmd.comando = CmdType::CMD_AUTO;
            else if (c == '2')         cmd.comando = CmdType::CMD_MANUAL;
            else if (c == 'r' || c == 'R') cmd.comando = CmdType::CMD_REARME;
            else if (c == 'w' || c == 'W') cmd.comando = CmdType::CMD_ACELERA;
            else if (c == 'a' || c == 'A') cmd.comando = CmdType::CMD_ESQUERDA;
            else if (c == 'd' || c == 'D') cmd.comando = CmdType::CMD_DIREITA;

            if (cmd.comando != CmdType::NENHUM) {
                send(sock, &cmd, sizeof(cmd), 0);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }

    configurar_terminal(false);
    close(sock);
    receiver.join();

    return 0;
}
