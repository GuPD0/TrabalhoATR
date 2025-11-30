#ifndef IPC_SHARED_HPP
#define IPC_SHARED_HPP

#include <cstdint>

const int PORTA_IPC = 9090;

// estado que o coletor envia para a Interface Local
struct EstadoDisplay {
    int pos_x;
    int pos_y;
    int angulo;
    int temperatura;
    bool falha_eletrica;
    bool falha_hidraulica;

    bool modo_automatico;
    bool com_defeito;
};

// comandos enviados da Interface Local → Coletor
enum class CmdType : int8_t {
    NENHUM = 0,
    CMD_AUTO = 1,
    CMD_MANUAL = 2,
    CMD_REARME = 3,
    CMD_ACELERA = 4,
    CMD_ESQUERDA = 5,
    CMD_DIREITA = 6
};

struct ComandoPacote {
    CmdType comando;
};

#endif
