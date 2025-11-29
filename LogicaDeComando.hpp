#ifndef LOGICA_DE_COMANDO_HPP
#define LOGICA_DE_COMANDO_HPP

#include "classes.hpp"

// Thread da tarefa Lógica de Comando.
// Lê itens do buffer, processa comandos do operador, responde a eventos de falha
// e publica atuadores via MQTT (publicar_atuadores) e também escreve Atuadores no buffer.
void LogicaDeComando(BufferCircular& buf);

#endif // LOGICA_DE_COMANDO_HPP
