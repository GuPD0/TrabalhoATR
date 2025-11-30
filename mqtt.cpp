#include "mqtt.hpp"
#include "MonitoramentoDeFalhas.hpp"
#include "classes.hpp"
#include "planejamento.hpp"

#include <mosquitto.h>
#include <iostream>
#include <string>
#include <mutex>
#include <atomic>


// Ponteiro global para o buffer principal
static BufferCircular* g_buffer = nullptr;

// Cliente MQTT
static mosquitto* g_mosq = nullptr;

// mutex + variáveis temporárias (montar pacote completo de sensores)
static std::mutex sensor_mutex;
static DadosSensores sensor_temp;

// Flags internas para montar pacote completo
static std::atomic<bool> got_x{false};
static std::atomic<bool> got_y{false};
static std::atomic<bool> got_ang{false};
static std::atomic<bool> got_temp{false};
static std::atomic<bool> got_felec{false};
static std::atomic<bool> got_fhid{false};

// Reset das flags internas
static void reset_flags() {
    got_x = got_y = got_ang = got_temp = got_felec = got_fhid = false;
}

// Verifica se o pacote completo foi recebido
static bool pacote_completo() {
    return got_x && got_y && got_ang && got_temp && got_felec && got_fhid;
}

// extrai o ID do caminhão
static int extrair_id(const std::string& topic)
{
    // Formato: truck/<id>/sensor/...
    size_t p1 = topic.find("truck/");
    if (p1 == std::string::npos) return -1;

    p1 += 6; // pula "truck/"

    size_t p2 = topic.find("/", p1);
    if (p2 == std::string::npos) return -1;

    return std::stoi(topic.substr(p1, p2 - p1));
}

// Callback de conexão
static void on_connect(struct mosquitto* mosq, void*, int rc)
{
    if (rc == 0) {
        std::cout << "[MQTT] Conectado ao broker.\n";

        mosquitto_subscribe(mosq, nullptr, "truck/+/sensor/+", 0);
        mosquitto_subscribe(mosq, nullptr, "truck/+/failure/+", 0);
        mosquitto_subscribe(mosq, nullptr, "truck/+/set_route", 0);
    } else {
        std::cerr << "[MQTT] Falha na conexão. RC=" << rc << "\n";
    }
}

// Callback de mensagem
static void on_message(struct mosquitto*, void*, const mosquitto_message* msg)
{
    std::string topic(msg->topic);
    std::string payload(static_cast<char*>(msg->payload), msg->payloadlen);
    int truck_id = extrair_id(topic);
    if (truck_id < 0) {
        std::cerr << "[MQTT] Erro: não foi possível extrair ID do tópico\n";
        return;
    }

    // Sensores normais
    {
        std::lock_guard<std::mutex> lock(sensor_mutex);
        try {
            // ---------------------- SENSORES DE POSIÇÃO ----------------------
            if (topic.find("i_posicao_x") != std::string::npos) {
                sensor_temp.pos_x = std::stoi(payload);
                got_x = true;
            }
            else if (topic.find("i_posicao_y") != std::string::npos) {
                sensor_temp.pos_y = std::stoi(payload);
                got_y = true;
            }
            else if (topic.find("i_angulo") != std::string::npos) {
                sensor_temp.angulo = std::stoi(payload);
                got_ang = true;
            }
            // ---------------------- SENSORES DE FALHA ------------------------
            else if (topic.find("i_temperatura") != std::string::npos) {
                sensor_temp.temperatura = std::stoi(payload);
                got_temp = true;

                // dispara eventos
                if (sensor_temp.temperatura > 120) {
                    std::lock_guard<std::mutex> lk(falhas_mutex);
                    falhas_caminhoes[truck_id].temperatura = true;
                }
            }
            else if (topic.find("i_falha_eletrica") != std::string::npos) {
                bool val = (payload == "true");
                sensor_temp.falha_eletrica = val;
                got_felec = true;

                if (val) {
                    std::lock_guard<std::mutex> lk(falhas_mutex);
                    falhas_caminhoes[truck_id].eletrica = true;
                }
            }
            else if (topic.find("i_falha_hidraulica") != std::string::npos) {
                bool val = (payload == "true");
                sensor_temp.falha_hidraulica = val;
                got_fhid = true;

                if (val) {
                    std::lock_guard<std::mutex> lk(falhas_mutex);
                    falhas_caminhoes[truck_id].hidraulica = true;
                }
            }
            // ---------------------- PACOTE COMPLETO --------------------------
            if (pacote_completo() && g_buffer) {
                g_buffer->push(sensor_temp);
                reset_flags();
            }
        }
        catch (...) {
            std::cerr << "[MQTT] Erro ao interpretar payload: " << payload << "\n";
        }
    }

    // --- PLANEJAMENTO DE ROTA ---
    if (topic.find("set_route") != std::string::npos) {
        try {
            size_t comma_pos = payload.find(',');
            if (comma_pos != std::string::npos) {
                int tx = std::stoi(payload.substr(0, comma_pos));
                int ty = std::stoi(payload.substr(comma_pos + 1));

                std::lock_guard<std::mutex> lock(mtx_missao);

                missao_atual.x_final = tx;
                missao_atual.y_final = ty;
                missao_atual.ativa   = true;

                std::cout << "[MQTT] Nova rota recebida → (" 
                          << tx << ", " << ty << ")\n";
            }
        }
        catch (...) {
            std::cerr << "[MQTT] Erro ao processar rota: " << payload << "\n";
        }
    }
}

// Inicialização
void iniciar_mqtt(BufferCircular& buf)
{
    g_buffer = &buf;

    mosquitto_lib_init();
    g_mosq = mosquitto_new("caminhao_client", true, nullptr);

    if (!g_mosq) {
        std::cerr << "[MQTT] Erro ao criar cliente.\n";
        return;
    }

    mosquitto_connect_callback_set(g_mosq, on_connect);
    mosquitto_message_callback_set(g_mosq, on_message);

    if (mosquitto_connect(g_mosq, "localhost", 1883, 60) != MOSQ_ERR_SUCCESS) {
        std::cerr << "[MQTT] Falha ao conectar ao broker.\n";
        return;
    }
}

// Loop não bloqueante (chamado pela thread da main)
void mqtt_loop()
{
    if (g_mosq)
        mosquitto_loop(g_mosq, 50, 1);
}

// Publicação de atuadores (usada pela Lógica de Comando)
void publicar_atuadores(int id, int aceleracao, int direcao)
{
    if (!g_mosq) return;

    std::string base = "truck/" + std::to_string(id) + "/actuator/";

    mosquitto_publish(g_mosq, nullptr,
                      (base + "o_aceleracao").c_str(),
                      sizeof(int), &aceleracao, 0, false);

    mosquitto_publish(g_mosq, nullptr,
                      (base + "o_direcao").c_str(),
                      sizeof(int), &direcao, 0, false);
}
