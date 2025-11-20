#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <string>
#include <cstdlib>  // Para rand()
#include <cmath>    // Para sin, cos

// Para simplicidade, estou usando uma estrutura básica; em produção, adicione tratamento de erros adequado.

extern "C" {
#include <mosquitto.h>
}

// Classe para representar um Caminhão com variáveis específicas e simulação física
class Truck {
public:
    int id;

    // Sensores (Inputs) - agora calculados/simulados
    int i_posicao_x;  // Posição no eixo x (GNSS)
    int i_posicao_y;  // Posição no eixo y (GNSS)
    int i_angulo;     // Ângulo do veículo (graus)

    // Tratamento de falha
    int i_temperatura;       // Temperatura do motor (-100 a +200)
    bool i_falha_eletrica;   // Falha elétrica
    bool i_falha_hidraulica; // Falha hidráulica

    // Atuadores (Outputs)
    int o_aceleracao;  // Aceleração em percentual (-100 a 100%)
    int o_direcao;     // Angulação em graus (-180 a 180)

    // Variáveis para simulação física
    double velocidade;  // Velocidade atual (m/s)
    double pos_x_real;  // Posição real x (para cálculo interno)
    double pos_y_real;  // Posição real y
    double angulo_real; // Ângulo real (radianos para cálculos)

    // Controle de defeito (para injeção via simulação)
    bool inject_defect;  // Se true, injeta defeito (ex.: falha elétrica)

    Truck(int truck_id) : id(truck_id),
                          i_posicao_x(0), i_posicao_y(0), i_angulo(0),
                          i_temperatura(25), i_falha_eletrica(false), i_falha_hidraulica(false),
                          o_aceleracao(0), o_direcao(0),
                          velocidade(0.0), pos_x_real(0.0), pos_y_real(0.0), angulo_real(0.0),
                          inject_defect(false) {}

    // Método para publicar dados do caminhão via MQTT
    void publishData(struct mosquitto* mosq) {
        std::string topic_base = "truck/" + std::to_string(id) + "/";

        // Publicar sensores (com ruído adicionado)
        mosquitto_publish(mosq, nullptr, (topic_base + "sensor/i_posicao_x").c_str(), sizeof(int), &i_posicao_x, 0, false);
        mosquitto_publish(mosq, nullptr, (topic_base + "sensor/i_posicao_y").c_str(), sizeof(int), &i_posicao_y, 0, false);
        mosquitto_publish(mosq, nullptr, (topic_base + "sensor/i_angulo").c_str(), sizeof(int), &i_angulo, 0, false);

        // Publicar tratamento de falha
        mosquitto_publish(mosq, nullptr, (topic_base + "failure/i_temperatura").c_str(), sizeof(int), &i_temperatura, 0, false);
        std::string falha_eletrica = i_falha_eletrica ? "true" : "false";
        mosquitto_publish(mosq, nullptr, (topic_base + "failure/i_falha_eletrica").c_str(), falha_eletrica.size(), falha_eletrica.c_str(), 0, false);
        std::string falha_hidraulica = i_falha_hidraulica ? "true" : "false";
        mosquitto_publish(mosq, nullptr, (topic_base + "failure/i_falha_hidraulica").c_str(), falha_hidraulica.size(), falha_hidraulica.c_str(), 0, false);

        // Publicar atuadores
        mosquitto_publish(mosq, nullptr, (topic_base + "actuator/o_aceleracao").c_str(), sizeof(int), &o_aceleracao, 0, false);
        mosquitto_publish(mosq, nullptr, (topic_base + "actuator/o_direcao").c_str(), sizeof(int), &o_direcao, 0, false);

        // Publicar alertas/defeitos baseados em temperatura
        std::string alerta_temp = (i_temperatura > 95) ? "ALERTA" : "NORMAL";
        mosquitto_publish(mosq, nullptr, (topic_base + "alert/temperatura").c_str(), alerta_temp.size(), alerta_temp.c_str(), 0, false);
        std::string defeito_temp = (i_temperatura > 120) ? "DEFEITO" : "OK";
        mosquitto_publish(mosq, nullptr, (topic_base + "defect/temperatura").c_str(), defeito_temp.size(), defeito_temp.c_str(), 0, false);
    }

    // Método para atualizar dados com simulação física e ruído
    void updateData(double dt = 1.0) {  // dt em segundos
        // Simulação física: Equação de diferenças para movimento
        // Aceleração em m/s² (assumindo 0-10 m/s² para 0-100%)
        double accel_real = (o_aceleracao / 100.0) * 10.0;  // Escala arbitrária
        // Direção em radianos
        angulo_real = (o_direcao * M_PI / 180.0);

        // Atualizar velocidade com inércia (simples: v = v + a * dt)
        velocidade += accel_real * dt;
        // Limitar velocidade (ex.: máxima 50 m/s)
        if (velocidade > 50.0) velocidade = 50.0;
        if (velocidade < -10.0) velocidade = -10.0;  // Reverso limitado

        // Atualizar posição
        pos_x_real += velocidade * dt * cos(angulo_real);
        pos_y_real += velocidade * dt * sin(angulo_real);

        // Converter para inteiros com ruído (média nula, desvio ~1-5 unidades)
        double ruido_x = (rand() % 11 - 5);  // Ruído -5 a +5
        double ruido_y = (rand() % 11 - 5);
        double ruido_ang = (rand() % 11 - 5) * 0.1;  // Ruído menor para ângulo
        i_posicao_x = static_cast<int>(pos_x_real + ruido_x);
        i_posicao_y = static_cast<int>(pos_y_real + ruido_y);
        i_angulo = static_cast<int>((angulo_real * 180.0 / M_PI) + ruido_ang);

        // Simular temperatura com ruído
        double ruido_temp = (rand() % 21 - 10);  // -10 a +10
        i_temperatura = 25 + (rand() % 51 - 25) + static_cast<int>(ruido_temp);  // Base 0-50 + ruído
        if (i_temperatura < -100) i_temperatura = -100;
        if (i_temperatura > 200) i_temperatura = 200;

        // Simular falhas (baixa probabilidade) ou injetar defeito
        i_falha_eletrica = inject_defect || ((rand() % 100) < 2);  // 2% chance ou injetado
        i_falha_hidraulica = (rand() % 100) < 2;

        // Resetar injeção de defeito após uso (ou manter se desejado)
        inject_defect = false;
    }

    // Método para injetar defeito (chamado via MQTT ou interface futura)
    void injectFailure() {
        inject_defect = true;
    }
};

// Classe para o Servidor MQTT (atualizada para adicionar caminhões via MQTT)
class MQTTServer {
private:
    std::vector<Truck> trucks;
    std::mutex mtx;
    struct mosquitto* mosq;
    int next_truck_id;  // Próximo ID para caminhão

    // Callback para mensagens recebidas (comandos para atuadores, injeção de defeito e adição de caminhão)
    static void on_message(struct mosquitto* mosq, void* obj, const struct mosquitto_message* msg) {
        MQTTServer* server = static_cast<MQTTServer*>(obj);
        std::string topic(msg->topic);
        std::string payload(static_cast<char*>(msg->payload), msg->payloadlen);

        // Processar comandos para atuadores: truck/{id}/actuator/{name} com payload numérico
        if (topic.find("/actuator/") != std::string::npos) {
            size_t truck_start = topic.find("truck/") + 6;
            size_t truck_end = topic.find("/", truck_start);
            int truck_id = std::stoi(topic.substr(truck_start, truck_end - truck_start));
            std::string actuator_name = topic.substr(topic.find_last_of("/") + 1);

            std::lock_guard<std::mutex> lock(server->mtx);
            for (auto& truck : server->trucks) {
                if (truck.id == truck_id) {
                    if (actuator_name == "o_aceleracao") {
                        int value = std::stoi(payload);
                        if (value >= -100 && value <= 100) {
                            truck.o_aceleracao = value;
                            std::cout << "Aceleração do caminhão " << truck_id << " definida para " << value << "%" << std::endl;
                        }
                    } else if (actuator_name == "o_direcao") {
                        int value = std::stoi(payload);
                        if (value >= -180 && value <= 180) {
                            truck.o_direcao = value;
                            std::cout << "Direção do caminhão " << truck_id << " definida para " << value << " graus" << std::endl;
                        }
                    }
                }
            }
        }
        // Comando para injetar defeito: truck/{id}/inject_defect com payload "true"
        else if (topic.find("/inject_defect") != std::string::npos) {
            size_t truck_start = topic.find("truck/") + 6;
            size_t truck_end = topic.find("/", truck_start);
            int truck_id = std::stoi(topic.substr(truck_start, truck_end - truck_start));

            std::lock_guard<std::mutex> lock(server->mtx);
            for (auto& truck : server->trucks) {
                if (truck.id == truck_id && payload == "true") {
                    truck.injectFailure();
                    std::cout << "Defeito injetado no caminhão " << truck_id << std::endl;
                }
            }
        }
        // Comando para adicionar caminhão: add_truck com payload sendo o ID (ou vazio para auto-incremento)
        else if (topic == "add_truck") {
            std::lock_guard<std::mutex> lock(server->mtx);
            int truck_id = server->next_truck_id++;
            if (!payload.empty()) {
                try {
                    truck_id = std::stoi(payload);
                } catch (...) {
                    // Se payload inválido, usar auto-incremento
                }
            }
            server->trucks.emplace_back(truck_id);
            std::cout << "Caminhão " << truck_id << " adicionado via MQTT." << std::endl;
        }
    }

public:
    MQTTServer() : next_truck_id(1) {
        mosquitto_lib_init();
        mosq = mosquitto_new(nullptr, true, this);
        if (!mosq) {
            std::cerr << "Erro ao criar cliente MQTT" << std::endl;
            exit(1);
        }
        mosquitto_message_callback_set(mosq, on_message);
        if (mosquitto_connect(mosq, "localhost", 1883, 60) != MOSQ_ERR_SUCCESS) {
            std::cerr << "Erro ao conectar ao broker MQTT" << std::endl;
            exit(1);
        }
        // Subscrever a tópicos de comandos para atuadores, injeção de defeito e adição de caminhão
        mosquitto_subscribe(mosq, nullptr, "truck/+/actuator/+", 0);
        mosquitto_subscribe(mosq, nullptr, "truck/+/inject_defect", 0);
        mosquitto_subscribe(mosq, nullptr, "add_truck", 0);
    }

    ~MQTTServer() {
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
    }

    // Loop principal do servidor com simulação física
    void run() {
        const double dt = 1.0;  // Passo de tempo em segundos
        while (true) {
            std::lock_guard<std::mutex> lock(mtx);
            for (auto& truck : trucks) {
                truck.updateData(dt);  // Atualizar com física e ruído
                truck.publishData(mosq);
            }
            mosquitto_loop(mosq, -1, 1);  // Processar mensagens MQTT
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(dt * 1000)));  // Aguardar dt segundos
        }
    }
};
