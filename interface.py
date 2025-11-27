import flet as ft
import socket
import time
import threading
import paho.mqtt.client as mqtt  # Para MQTT

class MQTTInterface:
    def __init__(self, page: ft.Page):
        self.page = page
        self.page.title = "Interface de Controle de Caminhões"
        self.page.theme_mode = ft.ThemeMode.LIGHT
        
        # Socket para comunicar com a simulação da mina (C++)
        self.sock = None
        
        # Lista de caminhões
        self.trucks = []
        
        # Cliente MQTT para receber dados
        self.mqtt_client = mqtt.Client()
        self.mqtt_client.on_message = self.on_mqtt_message
        try:
            self.mqtt_client.connect("localhost", 1883, 60)
            self.mqtt_client.subscribe("truck/+/sensor/+")  # Subscrever sensores
            self.mqtt_client.loop_start()  # Iniciar loop MQTT em thread
        except:
            print("Erro ao conectar MQTT")
        
        # Dicionário para armazenar dados recebidos (truck_id -> {sensor: value})
        self.truck_data = {}
        
        # Componentes da UI
        self.add_button = ft.ElevatedButton("Adicionar Caminhão", on_click=self.add_truck)
        self.close_button = ft.ElevatedButton("Fechar Interface", on_click=self.close_interface)
        self.reconnect_button = ft.ElevatedButton("Reconectar Socket", on_click=self.reconnect_socket)
        self.truck_count_label = ft.Text("N° de caminhões: 0", size=14, weight=ft.FontWeight.BOLD)
        self.error_label = ft.Text("", color=ft.Colors.RED, size=12)
        self.truck_list = ft.Column(scroll=ft.ScrollMode.AUTO, height=400)
        
        # Layout principal
        self.page.add(
            ft.Container(
                content=ft.Column([
                    ft.Row([self.add_button, self.reconnect_button, self.close_button], alignment=ft.MainAxisAlignment.SPACE_BETWEEN),
                    self.truck_count_label,
                    self.error_label,
                    ft.Text("Caminhões:", size=16, weight=ft.FontWeight.BOLD),
                    self.truck_list
                ]),
                padding=20
            )
        )
        
        self.page.on_close = self.on_close
        self.failure_buttons = {}  # Dicionário de listas para 3 botões por caminhão
        self.mode_buttons = {}
        self.manual_inputs = {}
        self.data_labels = {}
        
        # Tentar conectar socket
        self.connect_socket()
        
        # Thread para reconexão
        self.reconnect_thread = threading.Thread(target=self.auto_reconnect, daemon=True)
        self.reconnect_thread.start()
    
    def on_mqtt_message(self, client, userdata, msg):
        # Callback para mensagens MQTT
        topic = msg.topic
        payload = msg.payload.decode()
        parts = topic.split('/')
        if len(parts) >= 4 and parts[0] == 'truck':
            truck_id = int(parts[1])
            sensor = parts[3]
            if truck_id not in self.truck_data:
                self.truck_data[truck_id] = {}
            self.truck_data[truck_id][sensor] = payload
            # Atualizar UI
            self.page.run_thread(self.update_truck_data, truck_id)
    
    def update_truck_data(self, truck_id):
        # Atualizar label de dados
        if truck_id in self.data_labels:
            data = self.truck_data.get(truck_id, {})
            self.data_labels[truck_id].value = f"X: {data.get('i_posicao_x', 'N/A')}, Y: {data.get('i_posicao_y', 'N/A')}, Ang: {data.get('i_angulo', 'N/A')}"
            self.page.update()
    
    def connect_socket(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect(("localhost", 8080))
            self.error_label.value = "Conectado à simulação da mina."
            print("Conectado à simulação da mina via socket.")
        except Exception as e:
            self.error_label.value = f"Erro ao conectar socket: {str(e)}. Certifique-se de que a simulação C++ está rodando."
            print(self.error_label.value)
            self.sock = None
        self.page.update()
    
    def reconnect_socket(self, e=None):
        self.connect_socket()
    
    def auto_reconnect(self):
        while True:
            if not self.sock:
                self.connect_socket()
            time.sleep(5)
    
    def send_command(self, command):
        if self.sock:
            try:
                self.sock.sendall(command.encode())  # sendall garante envio completo
                self.error_label.value = "Comando enviado."
            except BrokenPipeError:
                self.error_label.value = "Conexão perdida. Tentando reconectar..."
                self.connect_socket()
                if self.sock:
                    try:
                        self.sock.sendall(command.encode())
                        self.error_label.value = "Comando reenviado."
                    except:
                        self.error_label.value = "Falha ao reenviar comando."
                else:
                    self.error_label.value = "Não foi possível reconectar."
        else:
            self.error_label.value = "Socket não conectado. Clique em 'Reconectar Socket'."
        self.page.update()
    
    def add_truck(self, e):
        self.send_command("add_truck")
        if self.sock:
            truck_id = len(self.trucks) + 1
            if truck_id not in self.trucks:
                self.trucks.append(truck_id)
                self.add_truck_to_gui(truck_id)
                self.update_count()
                self.page.update()
                print(f"Caminhão {truck_id} adicionado.")
    
    def close_interface(self, e):
        self.on_close(e)
        self.page.window_close()


    def add_truck_to_gui(self, truck_id):
        # Botões de falha próximos
        temp_button = ft.ElevatedButton(
            "Falha Temperatura",
            on_click=lambda e: self.inject_temp_failure(truck_id),
            style=ft.ButtonStyle(padding=ft.Padding(5,5,5,5))
        )
        electric_button = ft.ElevatedButton(
            "Falha Elétrica",
            on_click=lambda e: self.inject_electric_failure(truck_id),
            style=ft.ButtonStyle(padding=ft.Padding(5,5,5,5))
        )
        hydraulic_button = ft.ElevatedButton(
            "Falha Hidráulica",
            on_click=lambda e: self.inject_hydraulic_failure(truck_id),
            style=ft.ButtonStyle(padding=ft.Padding(5,5,5,5))
        )
        self.failure_buttons[truck_id] = [temp_button, electric_button, hydraulic_button]

        mode_button = ft.ElevatedButton(f"Modo: Automático - Caminhão {truck_id}", on_click=lambda e: self.toggle_mode(truck_id))
        self.mode_buttons[truck_id] = mode_button

        x_input = ft.TextField(label="Posição X (int)", width=100, visible=False)
        y_input = ft.TextField(label="Posição Y (int)", width=100, visible=False)
        ang_input = ft.TextField(label="Ângulo (-180 a 180)", width=120, visible=False)
        set_manual_button = ft.ElevatedButton("Definir Manual", on_click=lambda e: self.set_manual(truck_id, x_input, y_input, ang_input), visible=False)
        self.manual_inputs[truck_id] = (x_input, y_input, ang_input, set_manual_button)

        data_label = ft.Text(f"Dados: X: N/A, Y: N/A, Ang: N/A", size=12)
        self.data_labels[truck_id] = data_label

        controls_row = ft.Row([
            mode_button,
            ft.Row([temp_button, electric_button, hydraulic_button], spacing=5),  # botões de falha juntos
        ], alignment=ft.MainAxisAlignment.SPACE_BETWEEN, expand=True)

        self.truck_list.controls.append(
            ft.Column([
                ft.Text(f"Caminhão {truck_id}", size=14),
                controls_row,
                data_label,
                ft.Row([x_input, y_input, ang_input, set_manual_button])
            ])
        )
    
    def toggle_mode(self, truck_id):
        button = self.mode_buttons[truck_id]
        inputs = self.manual_inputs[truck_id]
        if "Automático" in button.text:
            button.text = f"Modo: Manual - Caminhão {truck_id}"
            for inp in inputs:
                inp.visible = True
            self.send_command(f"set_manual:{truck_id}:0:0:0")
        else:
            button.text = f"Modo: Automático - Caminhão {truck_id}"
            for inp in inputs:
                inp.visible = False
            self.send_command(f"set_auto:{truck_id}")
        self.page.update()
    
    def set_manual(self, truck_id, x_input, y_input, ang_input):
        try:
            x = int(x_input.value)
            y = int(y_input.value)
            ang = int(ang_input.value)
            if not (-180 <= ang <= 180):
                raise ValueError("Ângulo fora do range.")
            self.send_command(f"set_manual:{truck_id}:{x}:{y}:{ang}")
            print(f"Posição manual definida para caminhão {truck_id}: X={x}, Y={y}, Ângulo={ang}")
        except ValueError as e:
            self.error_label.value = f"Erro nos valores manuais para caminhão {truck_id}: {e}"
            self.page.update()
    
    def inject_temp_failure(self, truck_id):
        self.send_command(f"inject_temp_failure:{truck_id}")
        print(f"Falha de temperatura injetada no caminhão {truck_id}")
    
    def inject_electric_failure(self, truck_id):
        self.send_command(f"inject_electric_failure:{truck_id}")
        print(f"Falha elétrica injetada no caminhão {truck_id}")
    
    def inject_hydraulic_failure(self, truck_id):
        self.send_command(f"inject_hydraulic_failure:{truck_id}")
        print(f"Falha hidráulica injetada no caminhão {truck_id}")
    
    def update_count(self):
        self.truck_count_label.value = f"N° de caminhões: {len(self.trucks)}"
    
    def on_close(self, e):
        if self.sock:
            self.sock.close()
        self.mqtt_client.loop_stop()
        print("Interface fechada.")

def main(page: ft.Page):
    MQTTInterface(page)

if __name__ == "__main__":
    ft.app(target=main)
