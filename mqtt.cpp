#include <iostream>
#include <string>
#include <mosquitto.h>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

// Definição simplificada da estrutura DadosSensores
struct DadosSensores {
    int pos_x = 0;
    int pos_y = 0;
    int angulo = 0;
    int temperatura = 0;
    bool falha_eletrica = false;
    bool falha_hidraulica = false;
};

// Buffer Circular thread-safe para DadosSensores
class BufferCircular {
private:
    std::vector<DadosSensores> buffer;
    size_t capacidade;
    size_t inicio = 0;
    size_t fim = 0;
    size_t tamanho = 0;
    std::mutex mtx;
    std::condition_variable cv;

public:
    BufferCircular(size_t cap) : capacidade(cap), buffer(cap) {}

    void push(const DadosSensores& item) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]{ return tamanho < capacidade; });
        buffer[fim] = item;
        fim = (fim + 1) % capacidade;
        ++tamanho;
        cv.notify_all();
    }

    DadosSensores pop() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]{ return tamanho > 0; });
        DadosSensores item = buffer[inicio];
        inicio = (inicio + 1) % capacidade;
        --tamanho;
        cv.notify_all();
        return item;
    }
};

// Variáveis globais e mutex para sincronização
static struct mosquitto* mosq_caminhao = nullptr;
static BufferCircular* buffer_ptr = nullptr;
static std::mutex temp_mutex;
static DadosSensores sensores_temp;
static std::atomic<bool> pos_x_recebido(false), pos_y_recebido(false),
    angulo_recebido(false), temperatura_recebida(false),
    falha_eletrica_recebida(false), falha_hidraulica_recebida(false);

void reset_recebidos_flags() {
    pos_x_recebido = false;
    pos_y_recebido = false;
    angulo_recebido = false;
    temperatura_recebida = false;
    falha_eletrica_recebida = false;
    falha_hidraulica_recebida = false;
}

bool pacote_completo() {
    return pos_x_recebido && pos_y_recebido && angulo_recebido &&
           temperatura_recebida && falha_eletrica_recebida && falha_hidraulica_recebida;
}

void on_connect(struct mosquitto* mosq, void* obj, int rc) {
    if (rc == 0) {
        std::cout << "[MQTT] Conectado ao broker MQTT.\n";
        mosquitto_subscribe(mosq, nullptr, "truck/+/sensor/+", 0);
        mosquitto_subscribe(mosq, nullptr, "truck/+/failure/+", 0);
        mosquitto_subscribe(mosq, nullptr, "truck/+/actuator/+", 0);
        mosquitto_subscribe(mosq, nullptr, "truck/+/alert/+", 0);
        mosquitto_subscribe(mosq, nullptr, "truck/+/defect/+", 0);
    } else {
        std::cerr << "[MQTT] Falha na conexão, código: " << rc << std::endl;
    }
}

void on_message(struct mosquitto* mosq, void* obj, const struct mosquitto_message* msg) {
    std::string topic(msg->topic);
    std::string payload(static_cast<char*>(msg->payload), msg->payloadlen);

    bool enviar_buffer = false;

    {
        std::lock_guard<std::mutex> lock(temp_mutex);
        try {
            if (topic.find("i_posicao_x") != std::string::npos) {
                sensores_temp.pos_x = std::stoi(payload);
                pos_x_recebido = true;
            } else if (topic.find("i_posicao_y") != std::string::npos) {
                sensores_temp.pos_y = std::stoi(payload);
                pos_y_recebido = true;
            } else if (topic.find("i_angulo") != std::string::npos) {
                sensores_temp.angulo = std::stoi(payload);
                angulo_recebido = true;
            } else if (topic.find("i_temperatura") != std::string::npos) {
                sensores_temp.temperatura = std::stoi(payload);
                temperatura_recebida = true;
            } else if (topic.find("i_falha_eletrica") != std::string::npos) {
                sensores_temp.falha_eletrica = (payload == "true");
                falha_eletrica_recebida = true;
            } else if (topic.find("i_falha_hidraulica") != std::string::npos) {
                sensores_temp.falha_hidraulica = (payload == "true");
                falha_hidraulica_recebida = true;
            }
        } catch (...) {
            std::cerr << "[MQTT] ERRO AO PARSEAR PAYLOAD: " << payload << std::endl;
            return;
        }

        if (pacote_completo()) {
            if (buffer_ptr) {
                // Envia uma cópia segura para o buffer
                buffer_ptr->push(sensores_temp);
            }
            reset_recebidos_flags();
            enviar_buffer = true;
        }
    }
    std::cout << "[MQTT] Tópico: " << topic << " | Mensagem: " << payload;
    if (enviar_buffer) std::cout << " --> Pacote completo enviado";
    std::cout << std::endl;
}

void iniciar_mqtt(BufferCircular& buf) {
    buffer_ptr = &buf;

    mosquitto_lib_init();
    mosq_caminhao = mosquitto_new("caminhao", true, nullptr);
    if (!mosq_caminhao) {
        std::cerr << "Erro ao criar cliente MQTT" << std::endl;
        return;
    }

    mosquitto_connect_callback_set(mosq_caminhao, on_connect);
    mosquitto_message_callback_set(mosq_caminhao, on_message);

    if (mosquitto_connect(mosq_caminhao, "localhost", 1883, 60) != MOSQ_ERR_SUCCESS) {
        std::cerr << "Falha ao conectar ao broker MQTT" << std::endl;
        mosquitto_destroy(mosq_caminhao);
        mosq_caminhao = nullptr;
        return;
    }
}

void mqtt_loop_thread() {
    while (true) {
        if (mosq_caminhao) {
            int rc = mosquitto_loop(mosq_caminhao, 1000, 1);
            if (rc != MOSQ_ERR_SUCCESS) {
                std::cerr << "Erro mosquitto_loop: " << rc << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }
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

// Exemplo de uso na main()
int main() {
    BufferCircular buf(20);

    iniciar_mqtt(buf);

    std::thread mqtt_thread(mqtt_loop_thread);

    // Exemplo loop consumidor que processa dados do buffer
    while (true) {
        DadosSensores dados = buf.pop();
        std::cout << "Processando dados do caminhão: "
                  << "X=" << dados.pos_x << ", "
                  << "Y=" << dados.pos_y << ", "
                  << "Ang=" << dados.angulo << ", "
                  << "Temp=" << dados.temperatura << ", "
                  << "Falha Eletrica=" << (dados.falha_eletrica?"SIM":"NAO") << ", "
                  << "Falha Hidraulica=" << (dados.falha_hidraulica?"SIM":"NAO") 
                  << std::endl;
        // Aqui você pode fazer processamento adicional, publica atuadores, etc.
    }

    mqtt_thread.join();
    return 0;
}