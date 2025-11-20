import flet as ft
import socket
import time
import threading

class MQTTInterface:
    def __init__(self, page: ft.Page):
        self.page = page
        self.page.title = "Interface de Controle de Caminhões"
        self.page.theme_mode = ft.ThemeMode.LIGHT
        
        # Socket para comunicar com a simulação da mina (C++)
        self.sock = None
        
        # Lista de caminhões
        self.trucks = []
        
        # Componentes da UI
        self.add_button = ft.ElevatedButton("Adicionar Caminhão", on_click=self.add_truck)
        self.close_button = ft.ElevatedButton("Fechar Interface", on_click=self.close_interface)
        self.reconnect_button = ft.ElevatedButton("Reconectar Socket", on_click=self.reconnect_socket)
        self.truck_count_label = ft.Text("N° de caminhões: 0", size=14, weight=ft.FontWeight.BOLD)
        self.error_label = ft.Text("", color=ft.Colors.RED, size=12)  # Corrigido: ft.Colors.RED
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
        self.failure_buttons = {}
        self.mode_buttons = {}
        self.manual_inputs = {}
        
        # Tentar conectar após definir tudo
        self.connect_socket()
        
        # Thread para tentar reconectar periodicamente se falhar
        self.reconnect_thread = threading.Thread(target=self.auto_reconnect, daemon=True)
        self.reconnect_thread.start()
    
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
                self.sock.send(command.encode())
                self.error_label.value = "Comando enviado."
            except BrokenPipeError:
                self.error_label.value = "Conexão perdida. Tentando reconectar..."
                self.connect_socket()
                if self.sock:
                    try:
                        self.sock.send(command.encode())
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
        self.page.window_destroy()
    
    def add_truck_to_gui(self, truck_id):
        failure_button = ft.ElevatedButton(f"Gerar Falha - Caminhão {truck_id}", on_click=lambda e: self.inject_failure(truck_id))
        self.failure_buttons[truck_id] = failure_button
        
        mode_button = ft.ElevatedButton(f"Modo: Automático - Caminhão {truck_id}", on_click=lambda e: self.toggle_mode(truck_id))
        self.mode_buttons[truck_id] = mode_button
        
        x_input = ft.TextField(label="Posição X (int)", width=100, visible=False)
        y_input = ft.TextField(label="Posição Y (int)", width=100, visible=False)
        ang_input = ft.TextField(label="Ângulo (-180 a 180)", width=120, visible=False)
        set_manual_button = ft.ElevatedButton("Definir Manual", on_click=lambda e: self.set_manual(truck_id, x_input, y_input, ang_input), visible=False)
        
        self.manual_inputs[truck_id] = (x_input, y_input, ang_input, set_manual_button)
        
        self.truck_list.controls.append(
            ft.Column([
                ft.Row([
                    ft.Text(f"Caminhão {truck_id}", size=14),
                    mode_button,
                    failure_button
                ], alignment=ft.MainAxisAlignment.SPACE_BETWEEN),
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
    
    def inject_failure(self, truck_id):
        self.send_command(f"inject_failure:{truck_id}")
        print(f"Falha injetada no caminhão {truck_id}")
    
    def update_count(self):
        self.truck_count_label.value = f"N° de caminhões: {len(self.trucks)}"
    
    def on_close(self, e):
        if self.sock:
            self.sock.close()
        print("Interface fechada.")

def main(page: ft.Page):
    MQTTInterface(page)

if __name__ == "__main__":
    ft.app(target=main)
