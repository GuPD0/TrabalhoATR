#ifndef MQTT_HPP
#define MQTT_HPP
#include "classes.hpp"

// Inicializa o cliente MQTT do caminhão
void iniciar_mqtt(BufferCircular& buf);

// Loop do MQTT (não bloqueante)
void mqtt_loop();

void publicar_atuadores(int id, int aceleracao, int direcao);

#endif
