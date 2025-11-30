#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <string>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <algorithm>

extern "C" {
#include <mosquitto.h>
}

// small clamp util (C++17 portability)
template<typename T>
T clamp_custom(T v, T lo, T hi) {
    return (v < lo) ? lo : (v > hi ? hi : v);
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

    Truck(int truck_id = 1) :
        id(truck_id), manual_mode(false),
        i_posicao_x(0), i_posicao_y(0), i_angulo(0),
        i_temperatura(25), i_falha_eletrica(false), i_falha_hidraulica(false),
        o_aceleracao(0), o_direcao(0),
        velocidade(0.0), pos_x_real(0.0), pos_y_real(0.0), angulo_real(0.0),
        inject_defect(false)
    {}

    // publica sensores e flags (como string payloads) — compatível com coletor
    void publishData(struct mosquitto* mosq) {
        if (!mosq) return;
        std::string base = "truck/" + std::to_string(id) + "/";

        // Convertendo tudo para string (caminhão espera assim)
        std::string s_px  = std::to_string(i_posicao_x);
        std::string s_py  = std::to_string(i_posicao_y);
        std::string s_ang = std::to_string(i_angulo);
        std::string s_tmp = std::to_string(i_temperatura);

        std::string s_fe = i_falha_eletrica  ? "true" : "false";
        std::string s_fh = i_falha_hidraulica ? "true" : "false";

        // Sensores
        mosquitto_publish(mosq, nullptr, (base + "sensor/i_posicao_x").c_str(), s_px.size(), s_px.c_str(), 0, false);
        mosquitto_publish(mosq, nullptr, (base + "sensor/i_posicao_y").c_str(), s_py.size(), s_py.c_str(), 0, false);
        mosquitto_publish(mosq, nullptr, (base + "sensor/i_angulo").c_str(), s_ang.size(), s_ang.c_str(), 0, false);

        // Falhas
        mosquitto_publish(mosq, nullptr, (base + "failure/i_temperatura").c_str(), s_tmp.size(), s_tmp.c_str(), 0, false);
        mosquitto_publish(mosq, nullptr, (base + "failure/i_falha_eletrica").c_str(), s_fe.size(), s_fe.c_str(), 0, false);
        mosquitto_publish(mosq, nullptr, (base + "failure/i_falha_hidraulica").c_str(), s_fh.size(), s_fh.c_str(), 0, false);

    }
    // atualiza estado físico e sensores
    void updateData(double dt = 1.0) {
        if (manual_mode) {
            double rx = static_cast<double>((rand() % 11) - 5);
            double ry = static_cast<double>((rand() % 11) - 5);
            double rang = static_cast<double>((rand() % 11) - 5) * 0.1;
            double px = pos_x_real + rx;
            double py = pos_y_real + ry;

            px = clamp_custom(px, -5000.0, 5000.0);
            py = clamp_custom(py, -5000.0, 5000.0);

            i_posicao_x = static_cast<int>(px);
            i_posicao_y = static_cast<int>(py);

            i_angulo = static_cast<int>((angulo_real * 180.0 / M_PI) + rang);
        } else {
            double accel_real = (static_cast<double>(o_aceleracao) / 100.0) * 10.0;
            angulo_real = (static_cast<double>(o_direcao) * M_PI / 180.0);

            velocidade += accel_real * dt;
            velocidade = clamp_custom(velocidade, -10.0, 50.0);

            pos_x_real += velocidade * dt * cos(angulo_real);
            pos_y_real += velocidade * dt * sin(angulo_real);

            double rx = static_cast<double>((rand() % 11) - 5);
            double ry = static_cast<double>((rand() % 11) - 5);
            double rang = static_cast<double>((rand() % 11) - 5) * 0.1;

            double px = pos_x_real + rx;
            double py = pos_y_real + ry;

            px = clamp_custom(px, -5000.0, 5000.0);
            py = clamp_custom(py, -5000.0, 5000.0);

            i_posicao_x = static_cast<int>(px);
            i_posicao_y = static_cast<int>(py);
            i_angulo = static_cast<int>((angulo_real * 180.0 / M_PI) + rang);
        }
        if (!inject_defect) {
            double ruido_temp = static_cast<double>((rand() % 21) - 10);
            i_temperatura = 25 + ((rand() % 51) - 25) + static_cast<int>(ruido_temp);
            i_temperatura = clamp_custom(i_temperatura, -100, 200);

            i_falha_eletrica  = ((rand() % 100) < 2);
            i_falha_hidraulica = ((rand() % 100) < 2);
        } else {
            // se injetado, mantemos os valores que foram configurados por injeção
        }
        // reset temporário da injeção (injeção é pontual)
        inject_defect = false;
    }

    void setManualPosition(int x, int y, int ang) {
        if (ang < -180 || ang > 180) {
            std::cerr << "Angulo invalido para truck " << id << std::endl;
            return;
        }
        pos_x_real = static_cast<double>(x);
        pos_y_real = static_cast<double>(y);
        angulo_real = static_cast<double>(ang) * M_PI / 180.0;
        manual_mode = true;
    }
    void setAutoMode() { manual_mode = false; }
    void injectFailure() { inject_defect = true; }
    void injectTemp(int temp) { i_temperatura = temp; inject_defect = true; }
    void injectElectric() { i_falha_eletrica = true; inject_defect = true; }
    void injectHydraulic() { i_falha_hidraulica = true; inject_defect = true; }
};
// CLASSE SIMULAÇÃO DA MINA
class SimulacaoMina {
private:
    std::vector<Truck> trucks;
    std::mutex mtx;
    struct mosquitto* mosq;
    int next_truck_id;
    int server_socket;

    // extrai id do tópico "truck/<id>/..."
    static int extract_truck_id_from_topic(const std::string &topic) {
        size_t p = topic.find("truck/");
        if (p == std::string::npos) return -1;
        p += 6;
        size_t q = topic.find('/', p);
        if (q == std::string::npos) return -1;
        try {
            return std::stoi(topic.substr(p, q - p));
        } catch (...) {
            return -1;
        }
    }
    // callback MQTT (atuadores e injeções por tópico)
    static void on_message(struct mosquitto* mosq, void* obj, const struct mosquitto_message* msg) {
        SimulacaoMina* sim = static_cast<SimulacaoMina*>(obj);
        std::string topic(msg->topic);
        std::string payload(static_cast<char*>(msg->payload), msg->payloadlen);

        int tid = extract_truck_id_from_topic(topic);
        if (tid < 0) {
            // tópico que não segue o formato esperado; ignorar
            return;
        }

        // roteamento básico por sufixo de tópico
        if (topic.find("/actuator/") != std::string::npos) {
            std::string name = topic.substr(topic.find_last_of('/') + 1);
            try {
                int v = std::stoi(payload);
                std::lock_guard<std::mutex> lock(sim->mtx);
                for (auto &t : sim->trucks) {
                    if (t.id == tid) {
                        if (name == "o_aceleracao" && v >= -100 && v <= 100) t.o_aceleracao = v;
                        else if (name == "o_direcao" && v >= -180 && v <= 180) t.o_direcao = v;
                        break;
                    }
                }
            } catch (...) {
                // ignore malformed payload
            }
            return;
        }

        // injeções via mqtt: topics like
        // truck/<id>/inject_defect
        // truck/<id>/inject_temp_failure  payload: numeric temp
        // truck/<id>/inject_electric_failure  payload: "true"
        // truck/<id>/inject_hydraulic_failure  payload: "true"
        if (topic.find("/inject_defect") != std::string::npos) {
            if (payload == "true") {
                std::lock_guard<std::mutex> lock(sim->mtx);
                for (auto &t : sim->trucks) if (t.id == tid) { t.injectFailure(); break; }
            }
        } else if (topic.find("/inject_temp_failure") != std::string::npos) {
            try {
                int temp = std::stoi(payload);
                std::lock_guard<std::mutex> lock(sim->mtx);
                for (auto &t : sim->trucks) if (t.id == tid) { t.injectTemp(temp); break; }
            } catch (...) {}
        } else if (topic.find("/inject_electric_failure") != std::string::npos) {
            if (payload == "true") {
                std::lock_guard<std::mutex> lock(sim->mtx);
                for (auto &t : sim->trucks) if (t.id == tid) { t.injectElectric(); break; }
            }
        } else if (topic.find("/inject_hydraulic_failure") != std::string::npos) {
            if (payload == "true") {
                std::lock_guard<std::mutex> lock(sim->mtx);
                for (auto &t : sim->trucks) if (t.id == tid) { t.injectHydraulic(); break; }
            }
        } else if (topic.find("/set_manual") != std::string::npos) {
            // payload format "x,y,ang" (ex: "100,50,0")
            try {
                size_t p1 = payload.find(',');
                size_t p2 = payload.find(',', p1 + 1);
                if (p1 != std::string::npos && p2 != std::string::npos) {
                    int x = std::stoi(payload.substr(0, p1));
                    int y = std::stoi(payload.substr(p1 + 1, p2 - p1 - 1));
                    int ang = std::stoi(payload.substr(p2 + 1));
                    std::lock_guard<std::mutex> lock(sim->mtx);
                    for (auto &t : sim->trucks) if (t.id == tid) { t.setManualPosition(x, y, ang); break; }
                }
            } catch (...) {}
        } else if (topic.find("/set_auto") != std::string::npos) {
            std::lock_guard<std::mutex> lock(sim->mtx);
            for (auto &t : sim->trucks) if (t.id == tid) { t.setAutoMode(); break; }
        }
    }

public:
    SimulacaoMina() : mosq(nullptr), next_truck_id(1), server_socket(-1) {
        mosquitto_lib_init();
        mosq = mosquitto_new(nullptr, true, this);
        if (!mosq) {
            std::cerr << "Erro: não foi possível criar cliente mosquitto\n";
            exit(1);
        }

        mosquitto_message_callback_set(mosq, on_message);

        if (mosquitto_connect(mosq, "localhost", 1883, 60) != MOSQ_ERR_SUCCESS) {
            std::cerr << "Erro ao conectar ao broker MQTT\n";
            mosquitto_destroy(mosq);
            mosquitto_lib_cleanup();
            exit(1);
        }

        // Subscrever aos tópicos relevantes (atuadores já ouvidos pelo coletor; aqui injeções)
        mosquitto_subscribe(mosq, nullptr, "truck/+/actuator/+", 0);
        mosquitto_subscribe(mosq, nullptr, "truck/+/inject_defect", 0);
        mosquitto_subscribe(mosq, nullptr, "truck/+/inject_temp_failure", 0);
        mosquitto_subscribe(mosq, nullptr, "truck/+/inject_electric_failure", 0);
        mosquitto_subscribe(mosq, nullptr, "truck/+/inject_hydraulic_failure", 0);
        mosquitto_subscribe(mosq, nullptr, "truck/+/set_manual", 0);
        mosquitto_subscribe(mosq, nullptr, "truck/+/set_auto", 0);

        // servidor TCP para comandos simples (legado / teste)
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket < 0) {
            std::cerr << "Erro ao criar server socket\n";
            // mas continuamos sem o servidor
            server_socket = -1;
            return;
        }

        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(8080);

        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (bind(server_socket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "Warning: bind(8080) falhou (porta em uso?) — servidor de comando desabilitado\n";
            close(server_socket);
            server_socket = -1;
        } else {
            if (listen(server_socket, 5) < 0) {
                std::cerr << "Warning: listen(8080) falhou\n";
                close(server_socket);
                server_socket = -1;
            } else {
                std::cout << "Simulação da Mina: servidor de comandos ouvindo em 0.0.0.0:8080\n";
            }
        }
    }

    ~SimulacaoMina() {
        if (server_socket >= 0) close(server_socket);
        if (mosq) {
            mosquitto_disconnect(mosq);
            mosquitto_destroy(mosq);
            mosquitto_lib_cleanup();
        }
    }

    void addTruck(int id) {
        std::lock_guard<std::mutex> lock(mtx);
        trucks.emplace_back(id);
        std::cout << "Simulacao: Caminhão " << id << " adicionado\n";
    }

    // thread do servidor simples (aceita conexões e interpreta comandos)
    void startCommandServer() {
        if (server_socket < 0) return;

        std::thread([this]() {
            while (true) {
                int cli = accept(server_socket, nullptr, nullptr);
                if (cli < 0) { std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue; }
                char buf[1024]; std::memset(buf, 0, sizeof(buf));
                ssize_t n = read(cli, buf, sizeof(buf)-1);
                if (n > 0) {
                    std::string cmd(buf, static_cast<size_t>(n));
                    // trim newline/carriage returns
                    // Remove \r, \n e espaços extras
                    cmd.erase(std::remove(cmd.begin(), cmd.end(), '\r'), cmd.end());
                    cmd.erase(std::remove(cmd.begin(), cmd.end(), '\n'), cmd.end());
                    cmd.erase(std::remove(cmd.begin(), cmd.end(), '\0'), cmd.end());
                    cmd.erase(std::remove(cmd.begin(), cmd.end(), ' '), cmd.end());

                    if (cmd == "add_truck") {
                        addTruck(next_truck_id++);
                    } else if (cmd.rfind("inject_temp_failure:", 0) == 0) {
                        try {
                            int id = std::stoi(cmd.substr(19));
                            std::lock_guard<std::mutex> lock(mtx);
                            for (auto &t : trucks) if (t.id == id) { t.injectTemp(130); std::cout<<"Injetada temp failure em "<<id<<"\n"; break; }
                        } catch(...) {}
                    } else if (cmd.rfind("inject_electric_failure:",0) == 0) {
                        try {
                            int id = std::stoi(cmd.substr(24));
                            std::lock_guard<std::mutex> lock(mtx);
                            for (auto &t : trucks) if (t.id == id) { t.injectElectric(); std::cout<<"Injetada elec failure em "<<id<<"\n"; break; }
                        } catch(...) {}
                    } else if (cmd.rfind("inject_hydraulic_failure:",0) == 0) {
                        try {
                            int id = std::stoi(cmd.substr(25));
                            std::lock_guard<std::mutex> lock(mtx);
                            for (auto &t : trucks) if (t.id == id) { t.injectHydraulic(); std::cout<<"Injetada hidr failure em "<<id<<"\n"; break; }
                        } catch(...) {}
                    } else if (cmd.rfind("set_manual:", 0) == 0) {
                        // set_manual:<id>:<x>:<y>:<ang>
                        try {
                            size_t p1 = cmd.find(':', 11);
                            size_t p2 = cmd.find(':', p1+1);
                            size_t p3 = cmd.find(':', p2+1);
                            int id = std::stoi(cmd.substr(11, p1-11));
                            int x = std::stoi(cmd.substr(p1+1, p2-p1-1));
                            int y = std::stoi(cmd.substr(p2+1, p3-p2-1));
                            int ang = std::stoi(cmd.substr(p3+1));
                            std::lock_guard<std::mutex> lock(mtx);
                            for (auto &t : trucks) if (t.id == id) { t.setManualPosition(x,y,ang); break; }
                        } catch(...) {}
                    } else if (cmd.rfind("set_auto:",0) == 0) {
                        try {
                            int id = std::stoi(cmd.substr(9));
                            std::lock_guard<std::mutex> lock(mtx);
                            for (auto &t : trucks) if (t.id == id) { t.setAutoMode(); break; }
                        } catch(...) {}
                    } else {
                        std::cout << "Comando desconhecido: " << cmd << "\n";
                    }
                }
                close(cli);
            }
        }).detach();
    }

    void run() {
        // start server thread (if enabled)
        startCommandServer();

        // main loop
        const double dt = 1.0; // 1s update
        while (true) {
            {
                std::lock_guard<std::mutex> lock(mtx);
                for (auto &t : trucks) {
                    t.updateData(dt);
                    t.publishData(mosq);
                }
            }

            // process MQTT (blocking short time to handle callbacks)
            if (mosq) mosquitto_loop(mosq, 10, 1);

            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(dt * 1000)));
        }
    }
};
// main
int main() {
    std::cout << "Iniciando simulacao da mina...\n";
    SimulacaoMina sim;
    sim.run();
    return 0;
}