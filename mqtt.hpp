#ifndef MQTT_HPP
#define MQTT_HPP

#include "classes.hpp"

// Inicializa o cliente MQTT e registra callbacks
void iniciar_mqtt(BufferCircular& buf);

// Loop não bloqueante do MQTT (chamado pela thread)
void mqtt_loop();

// Publica atuadores da Lógica de Comando
void publicar_atuadores(int id, int aceleracao, int direcao);

#endif
