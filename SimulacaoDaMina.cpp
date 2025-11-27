#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <string>
#include <cstdlib>
#include <cmath>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

extern "C" {
#include <mosquitto.h>
}

class Truck {
public:
    int id;
    bool manual_mode;

    // Sensores (Inputs)
    int i_posicao_x;
    int i_posicao_y;
    int i_angulo;

    // Tratamento de falha
    int i_temperatura;           // Corrigido para int (temperatura real)
    bool i_falha_eletrica;
    bool i_falha_hidraulica;

    // Atuadores (Outputs)
    int o_aceleracao;
    int o_direcao;

    // Variáveis para simulação física (automático)
    double velocidade;
    double pos_x_real;
    double pos_y_real;
    double angulo_real;

    // Controle de defeito
    bool inject_defect;

    Truck(int truck_id) : id(truck_id), manual_mode(false),
        i_posicao_x(0), i_posicao_y(0), i_angulo(0),
        i_temperatura(25), i_falha_eletrica(false), i_falha_hidraulica(false),
        o_aceleracao(0), o_direcao(0),
        velocidade(0.0), pos_x_real(0.0), pos_y_real(0.0), angulo_real(0.0),
        inject_defect(false) {}

    void publishData(struct mosquitto* mosq) {
        std::string topic_base = "truck/" + std::to_string(id) + "/";
        mosquitto_publish(mosq, nullptr, (topic_base + "sensor/i_posicao_x").c_str(), sizeof(int), &i_posicao_x, 0, false);
        mosquitto_publish(mosq, nullptr, (topic_base + "sensor/i_posicao_y").c_str(), sizeof(int), &i_posicao_y, 0, false);
        mosquitto_publish(mosq, nullptr, (topic_base + "sensor/i_angulo").c_str(), sizeof(int), &i_angulo, 0, false);
        mosquitto_publish(mosq, nullptr, (topic_base + "failure/i_temperatura").c_str(), sizeof(int), &i_temperatura, 0, false);
        std::string falha_eletrica = i_falha_eletrica ? "true" : "false";
        mosquitto_publish(mosq, nullptr, (topic_base + "failure/i_falha_eletrica").c_str(), falha_eletrica.size(), falha_eletrica.c_str(), 0, false);
        std::string falha_hidraulica = i_falha_hidraulica ? "true" : "false";
        mosquitto_publish(mosq, nullptr, (topic_base + "failure/i_falha_hidraulica").c_str(), falha_hidraulica.size(), falha_hidraulica.c_str(), 0, false);
        mosquitto_publish(mosq, nullptr, (topic_base + "actuator/o_aceleracao").c_str(), sizeof(int), &o_aceleracao, 0, false);
        mosquitto_publish(mosq, nullptr, (topic_base + "actuator/o_direcao").c_str(), sizeof(int), &o_direcao, 0, false);
        std::string alerta_temp = (i_temperatura > 95) ? "ALERTA" : "NORMAL";
        mosquitto_publish(mosq, nullptr, (topic_base + "alert/temperatura").c_str(), alerta_temp.size(), alerta_temp.c_str(), 0, false);
        std::string defeito_temp = (i_temperatura > 120) ? "DEFEITO" : "OK";
        mosquitto_publish(mosq, nullptr, (topic_base + "defect/temperatura").c_str(), defeito_temp.size(), defeito_temp.c_str(), 0, false);
    }

    void updateData(double dt = 1.0) {
        if (manual_mode) {
            double ruido_x = (rand() % 11 - 5);
            double ruido_y = (rand() % 11 - 5);
            double ruido_ang = (rand() % 11 - 5) * 0.1;
            i_posicao_x = static_cast<int>(pos_x_real + ruido_x);
            i_posicao_y = static_cast<int>(pos_y_real + ruido_y);
            i_angulo = static_cast<int>((angulo_real * 180.0 / M_PI) + ruido_ang);
        } else {
            double accel_real = (o_aceleracao / 100.0) * 10.0;
            angulo_real = (o_direcao * M_PI / 180.0);
            velocidade += accel_real * dt;
            if (velocidade > 50.0) velocidade = 50.0;
            if (velocidade < -10.0) velocidade = -10.0;
            pos_x_real += velocidade * dt * cos(angulo_real);
            pos_y_real += velocidade * dt * sin(angulo_real);
            double ruido_x = (rand() % 11 - 5);
            double ruido_y = (rand() % 11 - 5);
            double ruido_ang = (rand() % 11 - 5) * 0.1;
            i_posicao_x = static_cast<int>(pos_x_real + ruido_x);
            i_posicao_y = static_cast<int>(pos_y_real + ruido_y);
            i_angulo = static_cast<int>((angulo_real * 180.0 / M_PI) + ruido_ang);
        }

        double ruido_temp = (rand() % 21 - 10);
        if (!inject_defect) {
            i_temperatura = 25 + (rand() % 51 - 25) + static_cast<int>(ruido_temp);
            if (i_temperatura < -100) i_temperatura = -100;
            if (i_temperatura > 200) i_temperatura = 200;
            i_falha_eletrica = ((rand() % 100) < 2);
            i_falha_hidraulica = ((rand() % 100) < 2);
        } 
        // Se existe injeção de falha, deixa os valores conforme setados
        // Por exemplo, temperatura já será fixada externamente

        inject_defect = false;
    }

    void setManualPosition(int x, int y, int ang) {
        if (ang < -180 || ang > 180) {
            std::cout << "Ângulo inválido para caminhão " << id << ". Deve ser entre -180 e 180." << std::endl;
            return;
        }
        pos_x_real = x;
        pos_y_real = y;
        angulo_real = ang * M_PI / 180.0;
        manual_mode = true;
    }

    void setAutoMode() {
        manual_mode = false;
    }

    void injectFailure() {
        inject_defect = true;
    }
};

class SimulacaoMina {
private:
    std::vector<Truck> trucks;
    std::mutex mtx;
    struct mosquitto* mosq;
    int next_truck_id;
    int server_socket;

    static void on_message(struct mosquitto* mosq, void* obj, const struct mosquitto_message* msg) {
        SimulacaoMina* sim = static_cast<SimulacaoMina*>(obj);
        std::string topic(msg->topic);
        std::string payload(static_cast<char*>(msg->payload), msg->payloadlen);
        if (topic.find("/actuator/") != std::string::npos) {
            size_t truck_start = topic.find("truck/") + 6;
            size_t truck_end = topic.find("/", truck_start);
            int truck_id = std::stoi(topic.substr(truck_start, truck_end - truck_start));
            std::string actuator_name = topic.substr(topic.find_last_of("/") + 1);
            std::lock_guard<std::mutex> lock(sim->mtx);
            for (auto& truck : sim->trucks) {
                if (truck.id == truck_id) {
                    if (actuator_name == "o_aceleracao") {
                        int value = std::stoi(payload);
                        if (value >= -100 && value <= 100) {
                            truck.o_aceleracao = value;
                        }
                    } else if (actuator_name == "o_direcao") {
                        int value = std::stoi(payload);
                        if (value >= -180 && value <= 180) {
                            truck.o_direcao = value;
                        }
                    }
                }
            }
        } else if (topic.find("/inject_defect") != std::string::npos) {
            size_t truck_start = topic.find("truck/") + 6;
            size_t truck_end = topic.find("/", truck_start);
            int truck_id = std::stoi(topic.substr(truck_start, truck_end - truck_start));
            std::lock_guard<std::mutex> lock(sim->mtx);
            for (auto& truck : sim->trucks) {
                if (truck.id == truck_id && payload == "true") {
                    truck.injectFailure();
                }
            }
        }
    }

public:
    SimulacaoMina() : next_truck_id(1) {
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
        mosquitto_subscribe(mosq, nullptr, "truck/+/actuator/+", 0);
        mosquitto_subscribe(mosq, nullptr, "truck/+/inject_defect", 0);

        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(8080);
        bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
        listen(server_socket, 5);
        std::cout << "Simulação da Mina ouvindo na porta 8080." << std::endl;
    }

    ~SimulacaoMina() {
        close(server_socket);
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
    }

    void addTruck(int id) {
        std::lock_guard<std::mutex> lock(mtx);
        trucks.emplace_back(id);
        std::cout << "Caminhão " << id << " adicionado." << std::endl;
    }

    void injectFailure(int truck_id) {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& truck : trucks) {
            if (truck.id == truck_id) {
                truck.injectFailure();
            }
        }
    }

    void setManualPosition(int truck_id, int x, int y, int ang) {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& truck : trucks) {
            if (truck.id == truck_id) {
                truck.setManualPosition(x, y, ang);
            }
        }
    }

    void setAutoMode(int truck_id) {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& truck : trucks) {
            if (truck.id == truck_id) {
                truck.setAutoMode();
            }
        }
    }

    void run() {
        std::thread socket_thread([this]() {
            while (true) {
                int client_socket = accept(server_socket, nullptr, nullptr);
                char buffer[1024] = {0};
                read(client_socket, buffer, 1024);
                std::string command(buffer);
                std::cout << "Comando recebido: " << command << std::endl;
                if (command == "add_truck") {
                    addTruck(next_truck_id++);
                }
                else if (command.find("inject_failure:") == 0) {
                    int truck_id = std::stoi(command.substr(15));
                    injectFailure(truck_id);
                }
                // Novos comandos para falhas específicas
                else if (command.find("inject_temp_failure:") == 0) {
                    int truck_id = std::stoi(command.substr(19));
                    std::lock_guard<std::mutex> lock(mtx);
                    for (auto& truck : trucks) {
                        if (truck.id == truck_id) {
                            truck.i_temperatura = 130; // valor alto para falha
                            std::cout << "Falha de temperatura injetada no caminhão " << truck_id << std::endl;
                        }
                    }
                }
                else if (command.find("inject_electric_failure:") == 0) {
                    int truck_id = std::stoi(command.substr(22));
                    std::lock_guard<std::mutex> lock(mtx);
                    for (auto& truck : trucks) {
                        if (truck.id == truck_id) {
                            truck.i_falha_eletrica = true;
                            std::cout << "Falha elétrica injetada no caminhão " << truck_id << std::endl;
                        }
                    }
                }
                else if (command.find("inject_hydraulic_failure:") == 0) {
                    int truck_id = std::stoi(command.substr(24));
                    std::lock_guard<std::mutex> lock(mtx);
                    for (auto& truck : trucks) {
                        if (truck.id == truck_id) {
                            truck.i_falha_hidraulica = true;
                            std::cout << "Falha hidráulica injetada no caminhão " << truck_id << std::endl;
                        }
                    }
                }
                else if (command.find("set_manual:") == 0) {
                    size_t pos1 = command.find(":", 11);
                    size_t pos2 = command.find(":", pos1 + 1);
                    size_t pos3 = command.find(":", pos2 + 1);
                    int truck_id = std::stoi(command.substr(11, pos1 - 11));
                    int x = std::stoi(command.substr(pos1 + 1, pos2 - pos1 - 1));
                    int y = std::stoi(command.substr(pos2 + 1, pos3 - pos2 - 1));
                    int ang = std::stoi(command.substr(pos3 + 1));
                    setManualPosition(truck_id, x, y, ang);
                }
                else if (command.find("set_auto:") == 0) {
                    int truck_id = std::stoi(command.substr(9));
                    setAutoMode(truck_id);
                }
                close(client_socket);
            }
        });

        const double dt = 1.0;
        while (true) {
            std::lock_guard<std::mutex> lock(mtx);
            for (auto& truck : trucks) {
                truck.updateData(dt);
                truck.publishData(mosq);
            }
            mosquitto_loop(mosq, -1, 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(dt * 1000)));
        }
        socket_thread.join();
    }
};

int main() {
    std::cout << "Iniciando simulação da mina..." << std::endl;    
    SimulacaoMina simulacao;
    simulacao.run();  // Loop infinito    
    return 0;
}
