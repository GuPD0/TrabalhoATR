#include <iostream>
#include <string>
#include <mosquitto.h>

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
    std::cout << "Tópico: " << topic << " | Mensagem: " << payload << std::endl;
}
