import flet as ft
import paho.mqtt.client as mqtt
import threading
import subprocess  # Se estiver usando subprocesso para o servidor C++
import time

class MQTTInterface:
    def __init__(self, page: ft.Page):
        self.page = page
        self.page.title = "Interface de Controle de Caminhões"
        self.page.theme_mode = ft.ThemeMode.LIGHT
        
        # Flag para controlar se o app está rodando
        self.running = True
        
        # Cliente MQTT
        self.client = mqtt.Client()
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.client.connect("localhost", 1883, 60)
        self.client.loop_start()  # Iniciar loop em thread separada
        
        # Subprocesso para servidor C++ (se usado; remova se não)
        self.server_process = None
        # self.server_process = subprocess.Popen(['./mqtt_server'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        # print("Servidor C++ iniciado.")
        # time.sleep(2)
        
        # Lista de caminhões (IDs)
        self.trucks = []
        
        # Componentes da UI
        self.add_button = ft.ElevatedButton("Adicionar Caminhão", on_click=self.add_truck)
        self.truck_count_label = ft.Text("N° de caminhões: 0", size=14, weight=ft.FontWeight.BOLD)
        self.truck_list = ft.Column(scroll=ft.ScrollMode.AUTO, height=300)
        
        # Layout principal
        self.page.add(
            ft.Container(
                content=ft.Column([
                    self.add_button,
                    self.truck_count_label,
                    ft.Text("Caminhões:", size=16, weight=ft.FontWeight.BOLD),
                    self.truck_list
                ]),
                padding=20
            )
        )
        
        # Manipulador para fechamento da página
        self.page.on_close = self.on_close
        
        # Dicionário para armazenar botões de falha por ID
        self.failure_buttons = {}
    
    def on_connect(self, client, userdata, flags, rc):
        print("Conectado ao MQTT Broker")
        self.client.subscribe("truck/+/sensor/i_posicao_x")
    
    def on_message(self, client, userdata, msg):
        if not self.running:
            return  # Ignorar mensagens se o app estiver fechando
        topic = msg.topic
        print(f"Mensagem recebida: {topic} = {msg.payload.decode()}")
        if topic.startswith("truck/") and "/sensor/i_posicao_x" in topic:
            parts = topic.split("/")
            if len(parts) >= 3:
                try:
                    truck_id = int(parts[1])
                    if truck_id not in self.trucks:
                        self.trucks.append(truck_id)
                        self.add_truck_to_gui(truck_id)
                        self.update_count()
                        self.page.update()
                        print(f"Caminhão {truck_id} detectado e adicionado à UI.")
                except ValueError:
                    print("Erro ao extrair ID do caminhão.")
    
    def add_truck(self, e):
        self.client.publish("add_truck", "")
        print("Publicado 'add_truck'.")
        truck_id = len(self.trucks) + 1
        if truck_id not in self.trucks:
            self.trucks.append(truck_id)
            self.add_truck_to_gui(truck_id)
            self.update_count()
            self.page.update()
            print(f"Caminhão {truck_id} adicionado imediatamente à UI.")
    
    def add_truck_to_gui(self, truck_id):
        failure_button = ft.ElevatedButton(f"Gerar Falha - Caminhão {truck_id}", on_click=lambda e: self.inject_failure(truck_id))
        self.failure_buttons[truck_id] = failure_button
        self.truck_list.controls.append(
            ft.Row([
                ft.Text(f"Caminhão {truck_id}", size=14),
                failure_button
            ], alignment=ft.MainAxisAlignment.SPACE_BETWEEN)
        )
    
    def inject_failure(self, truck_id):
        self.client.publish(f"truck/{truck_id}/inject_defect", "true")
        print(f"Falha injetada no caminhão {truck_id}")
    
    def update_count(self):
        self.truck_count_label.value = f"N° de caminhões: {len(self.trucks)}"
    
    def on_close(self, e):
        # Manipulador para fechamento da página
        print("Fechando a interface...")
        self.running = False  # Parar processamento de mensagens
        self.client.loop_stop()  # Parar loop MQTT
        self.client.disconnect()  # Desconectar MQTT
        if self.server_process:
            self.server_process.terminate()  # Matar subprocesso C++
            self.server_process.wait()
        print("Interface fechada com sucesso.")
        self.page.window_destroyed()  # Forçar fechamento

def main(page: ft.Page):
    MQTTInterface(page)

if __name__ == "__main__":
    ft.app(target=main)
