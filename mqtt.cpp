#include <iostream>
#include <string>
#include <mosquitto.h>
#include "mqtt.hpp"
#include "classes.hpp"
#include <atomic>

static struct mosquitto* mosq_caminhao = nullptr;
static BufferCircular* buffer_ptr = nullptr;

// Estado temporário do último pacote de sensores recebido
static DadosSensores sensores_temp;

void on_connect(struct mosquitto* mosq, void* obj, int rc) {
    if (rc == 0) {
        std::cout << "Conectado ao broker MQTT." << std::endl;
        // Subscrever a tópicos dos caminhões
        mosquitto_subscribe(mosq, nullptr, "truck/+/sensor/+", 0);  // Sensores
        mosquitto_subscribe(mosq, nullptr, "truck/+/failure/+", 0);  // Falhas
        mosquitto_subscribe(mosq, nullptr, "truck/+/actuator/+", 0);  // Atuadores
        mosquitto_subscribe(mosq, nullptr, "truck/+/alert/+", 0);  // Alertas
        mosquitto_subscribe(mosq, nullptr, "truck/+/defect/+", 0);  // Defeitos
    } else {
        std::cout << "Falha na conexão: " << rc << std::endl;
    }
}

void on_message(struct mosquitto* mosq, void* obj, const struct mosquitto_message* msg) {
    std::string topic(msg->topic);
    std::string payload(static_cast<char*>(msg->payload), msg->payloadlen);
    // --------- PARSER DOS SENSORES MQTT ---------
    try {
        if (topic.find("i_posicao_x") != std::string::npos) {
            sensores_temp.pos_x = std::stoi(payload);
        }
        else if (topic.find("i_posicao_y") != std::string::npos) {
            sensores_temp.pos_y = std::stoi(payload);
        }
        else if (topic.find("i_angulo") != std::string::npos) {
            sensores_temp.angulo = std::stoi(payload);
        }
        else if (topic.find("i_temperatura") != std::string::npos) {
            sensores_temp.temperatura = std::stoi(payload);
        }
        else if (topic.find("i_falha_eletrica") != std::string::npos) {
            sensores_temp.falha_eletrica = (payload == "true");
        }
        else if (topic.find("i_falha_hidraulica") != std::string::npos) {
            sensores_temp.falha_hidraulica = (payload == "true");
        }
    } catch (...) {
        std::cout << "[MQTT] ERRO AO PARSEAR PAYLOAD: " << payload << std::endl;
    }
    // ------- IDENTIFICAÇÃO BÁSICA DO SENSOR RECEBIDO -------
    bool eh_pos_x = topic.find("i_posicao_x") != std::string::npos;
    bool eh_pos_y = topic.find("i_posicao_y") != std::string::npos;
    bool eh_ang   = topic.find("i_angulo")    != std::string::npos;
    bool eh_temp  = topic.find("i_temperatura") != std::string::npos;
    bool eh_eletr = topic.find("i_falha_eletrica") != std::string::npos;
    bool eh_hidr  = topic.find("i_falha_hidraulica") != std::string::npos;

    // Apenas mostrando o que chegou (para debug)
    if (eh_pos_x)  std::cout << "[MQTT] pos_x = "  << payload << std::endl;
    if (eh_pos_y)  std::cout << "[MQTT] pos_y = "  << payload << std::endl;
    if (eh_ang)    std::cout << "[MQTT] ang = "    << payload << std::endl;
    if (eh_temp)   std::cout << "[MQTT] temp = "   << payload << std::endl;
    if (eh_eletr)  std::cout << "[MQTT] falha_eletrica = " << payload << std::endl;
    if (eh_hidr)   std::cout << "[MQTT] falha_hidraulica = " << payload << std::endl;

    // --------- ENVIA O PACOTE DE SENSORES PARA O BUFFER ---------
    if (topic.find("sensor") != std::string::npos || 
        topic.find("failure") != std::string::npos) 
    {
        if (buffer_ptr) {
            buffer_ptr->push(sensores_temp);
        }
    }
    std::cout << "Tópico: " << topic << " | Mensagem: " << payload << std::endl;
}

void iniciar_mqtt(BufferCircular& buf) {
    buffer_ptr = &buf;

    mosquitto_lib_init();
    mosq_caminhao = mosquitto_new("caminhao", true, nullptr);

    if (!mosq_caminhao) {
        std::cerr << "Erro ao criar cliente MQTT do caminhão" << std::endl;
        return;
    }

    mosquitto_connect(mosq_caminhao, "localhost", 1883, 60);

    // Subscrições do caminhão aos sensores
    mosquitto_subscribe(mosq_caminhao, nullptr, "truck/+/sensor/+", 0);
    mosquitto_subscribe(mosq_caminhao, nullptr, "truck/+/failure/+", 0);

    // Configurar callback
    mosquitto_message_callback_set(mosq_caminhao, on_message);
}

void mqtt_loop() {
    if (mosq_caminhao)
        mosquitto_loop(mosq_caminhao, 0, 1);
}

void publicar_atuadores(int id, int aceleracao, int direcao) {
    if (!mosq_caminhao) return;

    std::string base = "truck/" + std::to_string(id) + "/actuator/";

    mosquitto_publish(mosq_caminhao, nullptr,
        (base + "o_aceleracao").c_str(),
        sizeof(int), &aceleracao, 0, false
    );

    mosquitto_publish(mosq_caminhao, nullptr,
        (base + "o_direcao").c_str(),
        sizeof(int), &direcao, 0, false
    );
}
