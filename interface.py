import flet as ft
import socket
import time
import threading
import paho.mqtt.client as mqtt
import random

class MQTTInterface:
    # Configuração do mapa / coordenação
    MAP_W = 760
    MAP_H = 520
    COORD_MAX_X = 100  # coordenadas lógicas X vão de 0..100
    COORD_MAX_Y = 100  # coordenadas lógicas Y vão de 0..100

    def __init__(self, page: ft.Page):
        self.pellets = []   # lista de bolinhas no mapa
        self.pellet_size = 10
        self.page = page
        self.page.title = "Interface de Controle de Caminhões"
        self.page.theme_mode = ft.ThemeMode.LIGHT

        # Socket para comunicar com a simulação da mina (C++)
        self.sock = None

        # Lista de caminhões
        self.trucks = []

        # Mapa: dicionário truck_id -> Container (ícone no mapa)
        self.map_truck_icons = {}

        # Dados recebidos via MQTT (truck_id -> {sensor: value})
        self.truck_data = {}

        # Cliente MQTT
        self.mqtt_client = mqtt.Client()
        self.mqtt_client.on_message = self.on_mqtt_message
        try:
            self.mqtt_client.connect("localhost", 1883, 60)
            self.mqtt_client.subscribe("truck/+/sensor/+")
            self.mqtt_client.loop_start()
        except Exception as e:
            print("Erro ao conectar MQTT:", e)

        # Componentes da UI (controle esquerdo)
        self.add_button = ft.ElevatedButton("Adicionar Caminhão", on_click=self.add_truck)
        self.close_button = ft.ElevatedButton("Fechar Interface", on_click=self.close_interface)
        self.reconnect_button = ft.ElevatedButton("Reconectar Socket", on_click=self.reconnect_socket)
        self.truck_count_label = ft.Text("N° de caminhões: 0", size=14, weight=ft.FontWeight.BOLD)
        self.error_label = ft.Text("", color=ft.Colors.RED, size=12)
        self.truck_list = ft.Column(scroll=ft.ScrollMode.AUTO, height=400)

        # Dicionários por caminhão
        self.failure_buttons = {}
        self.mode_buttons = {}
        self.manual_inputs = {}
        self.data_labels = {}

        # Criar mapa Pac-Man (estilo B: minimalista — paredes sem bolinhas)
        # create_pacman_map define self.map_stack (Stack) e retorna o container a ser colocado na UI
        self.map_container = self.create_pacman_map()

        # Layout principal: controles à esquerda, mapa à direita
        controls_column = ft.Column([
            ft.Row([self.add_button, self.reconnect_button, self.close_button], alignment=ft.MainAxisAlignment.SPACE_BETWEEN),
            self.truck_count_label,
            self.error_label,
            ft.Text("Caminhões:", size=16, weight=ft.FontWeight.BOLD),
            self.truck_list
        ], spacing=10, width=340)

        self.page.add(
            ft.Container(
                content=ft.Column(
                    [
                        controls_column,
                        ft.Divider(height=20),
                        self.map_container
                    ],
                    alignment=ft.MainAxisAlignment.START
                ),
                padding=10
            )
        )


        # on_close handler seguro
        self.page.on_close = self.on_close

        # conectar socket e iniciar threads
        self.connect_socket()
        threading.Thread(target=self.auto_reconnect, daemon=True).start()
        threading.Thread(target=self.auto_move_loop, daemon=True).start()

    # ---------------- MQTT callbacks ----------------
    def on_mqtt_message(self, client, userdata, msg):
        topic = msg.topic
        payload = msg.payload.decode()
        parts = topic.split('/')
        if len(parts) >= 4 and parts[0] == 'truck':
            try:
                truck_id = int(parts[1])
            except:
                return
            sensor = parts[3]
            if truck_id not in self.truck_data:
                self.truck_data[truck_id] = {}
            self.truck_data[truck_id][sensor] = payload
            # Atualizar UI (usar run_async para garantir thread-safe)
            self.page.run_async(self.update_truck_data, truck_id)

    def update_truck_data(self, truck_id):
        # Atualiza label de dados e move ícone no mapa se posição disponível
        if truck_id in self.data_labels:
            data = self.truck_data.get(truck_id, {})
            x = data.get('i_posicao_x')
            y = data.get('i_posicao_y')
            ang = data.get('i_angulo', 'N/A')
            self.data_labels[truck_id].value = f"X: {x if x is not None else 'N/A'}, Y: {y if y is not None else 'N/A'}, Ang: {ang}"
            try:
                if x is not None and y is not None:
                    px = float(x)
                    py = float(y)
                    self.move_truck_on_map(truck_id, px, py)
            except Exception:
                pass
            self.page.update()

    # ---------------- Socket ----------------
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
                self.sock.sendall(command.encode())
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

    # ---------------- Gerenciamento de caminhões e UI ----------------
    def add_truck(self, e):
        # solicita simulação a adicionar caminhão
        self.send_command("add_truck")
        truck_id = len(self.trucks) + 1
        # adicionar mesmo se socket ausente (útil para testes)
        self.trucks.append(truck_id)
        self.add_truck_to_gui(truck_id)
        # posição inicial central do mapa (lógicas 0..100)
        self.move_truck_on_map(truck_id, self.COORD_MAX_X/2, self.COORD_MAX_Y/2)
        self.update_count()
        self.page.update()
        print(f"Caminhão {truck_id} adicionado.")

    def close_interface(self, e):
        # chamar on_close e fechar janela
        try:
            self.on_close(e)
        except:
            pass
        self.page.window_close()

    def add_pellet(self, x, y):
        # verifica colisão com bolinhas já existentes
        for px, py, icon in self.pellets:
            if abs(px - x) < self.pellet_size*2 and abs(py - y) < self.pellet_size*2:
                print("Colisão: já existe pellet nessa área!")
                return  # não adiciona

        pellet = ft.Container(
            width=self.pellet_size,
            height=self.pellet_size,
            bgcolor=ft.Colors.WHITE,
            border_radius=ft.border_radius.all(50),
            left=x,
            top=y
        )

        self.map_stack.controls.append(pellet)
        self.pellets.append((x, y, pellet))
        self.page.update()


    def add_truck_to_gui(self, truck_id):
        # Botões de falha
        temp_button = ft.ElevatedButton("Falha Temperatura", on_click=lambda e: self.inject_temp_failure(truck_id))
        electric_button = ft.ElevatedButton("Falha Elétrica", on_click=lambda e: self.inject_electric_failure(truck_id))
        hydraulic_button = ft.ElevatedButton("Falha Hidráulica", on_click=lambda e: self.inject_hydraulic_failure(truck_id))
        self.failure_buttons[truck_id] = [temp_button, electric_button, hydraulic_button]

        # Botão modo
        mode_button = ft.ElevatedButton(f"Modo: Automático - Caminhão {truck_id}", on_click=lambda e: self.toggle_mode(truck_id))
        self.mode_buttons[truck_id] = mode_button

        # Inputs manuais (invisíveis até ativar manual)
        x_input = ft.TextField(label="Posição X (int)", width=100, visible=False)
        y_input = ft.TextField(label="Posição Y (int)", width=100, visible=False)
        ang_input = ft.TextField(label="Ângulo (-180 a 180)", width=120, visible=False)
        set_manual_button = ft.ElevatedButton("Definir Manual", on_click=lambda e: self.set_manual(truck_id, x_input, y_input, ang_input), visible=False)
        self.manual_inputs[truck_id] = (x_input, y_input, ang_input, set_manual_button)

        # Label de dados
        data_label = ft.Text(f"Dados: X: N/A, Y: N/A, Ang: N/A", size=12)
        self.data_labels[truck_id] = data_label

        # Adicionar controles à lista
        controls_row = ft.Row([mode_button, ft.Row([temp_button, electric_button, hydraulic_button], spacing=5)], alignment=ft.MainAxisAlignment.SPACE_BETWEEN, expand=True)
        self.truck_list.controls.append(
            ft.Column([
                ft.Text(f"Caminhão {truck_id}", size=14),
                controls_row,
                data_label,
                ft.Row([x_input, y_input, ang_input, set_manual_button])
            ])
        )

        # criar ícone no mapa (Container com top/left)
        truck_icon = ft.Container(
            content=ft.Text(str(truck_id), size=12, weight=ft.FontWeight.BOLD),
            width=22,
            height=22,
            alignment=ft.alignment.center,
            bgcolor=ft.Colors.YELLOW_700,
            border_radius=ft.border_radius.all(11),
            tooltip=f"Caminhão {truck_id}",
            top=0,
            left=0
        )

        # salvar referência e adicionar ao stack do mapa
        self.map_truck_icons[truck_id] = truck_icon
        self.map_stack.controls.append(truck_icon)
        # atualizar UI
        self.page.update()

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
            # aplicar localmente também (atualiza ícone)
            self.move_truck_on_map(truck_id, x, y)
        except Exception as e:
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
        self.page.update()

    # ---------------- Mapa / utilitários ----------------
    def create_pacman_map(self):
        """
        Cria o mapa estilo Pac-Man (minimalista):
         - fundo escuro
         - paredes azuis (labirinto simples)
         - sem pellets (opção B: minimalista)
        """

        # Stack interno onde colocamos paredes e sprites (suporta left/top em containers)
        self.map_stack = ft.Stack(width=self.MAP_W, height=self.MAP_H, controls=[])

        # container externo aplica o fundo escuro
        map_container = ft.Container(
            content=self.map_stack,
            width=self.MAP_W,
            height=self.MAP_H,
            bgcolor=ft.Colors.BLACK
        )

        # paredes externas
        wall_thickness = 16
        outer_walls = [
            (0, 0, self.MAP_W, wall_thickness),
            (0, self.MAP_H - wall_thickness, self.MAP_W, wall_thickness),
            (0, 0, wall_thickness, self.MAP_H),
            (self.MAP_W - wall_thickness, 0, wall_thickness, self.MAP_H),
        ]
        for x, y, w, h in outer_walls:
            self.map_stack.controls.append(
                ft.Container(top=y, left=x, width=w, height=h, bgcolor=ft.Colors.BLUE_900)
            )

        # algumas paredes internas (labirinto simplificado — ajuste livremente)
        internal_walls = [
            (60, 60, 640, 16),
            (60, 120, 240, 16),
            (580, 120, 120, 16),
            (300, 200, 160, 16),
            (120, 260, 520, 16),
            (60, 340, 200, 16),
            (500, 340, 200, 16),
            (300, 400, 160, 16),
        ]
        for x, y, w, h in internal_walls:
            self.map_stack.controls.append(
                ft.Container(top=y, left=x, width=w, height=h, bgcolor=ft.Colors.BLUE_900)
            )

        return map_container

    def logical_to_pixel(self, x, y):
        """Converte coordenadas lógicas (0..COORD_MAX) para pixels do mapa"""
        px = (x / self.COORD_MAX_X) * (self.MAP_W - 32) + 8  # margem pequena
        py = (y / self.COORD_MAX_Y) * (self.MAP_H - 32) + 8
        px = max(8, min(px, self.MAP_W - 32))
        py = max(8, min(py, self.MAP_H - 32))
        return int(px), int(py)

    def move_truck_on_map(self, truck_id, x, y):
        """
        Move o ícone do caminhão no mapa para as coordenadas lógicas x,y.
        """
        if truck_id not in self.map_truck_icons:
            return
        px, py = self.logical_to_pixel(x, y)
        icon = self.map_truck_icons[truck_id]
        icon.left = px
        icon.top = py

        # colisão com pellets
        for (px_p, py_p, pellet) in self.pellets:
            if abs(px - px_p) < self.pellet_size and abs(py - py_p) < self.pellet_size:
                print(f"COLISÃO: Caminhão {truck_id} bateu em uma bolinha!")
                return  # impede mover

        # atualizar UI (thread-safe)
        try:
            self.page.update()
        except Exception:
            self.page.run_async(lambda: self.page.update())

    # ---------------- Modo automático local ----------------
    def auto_move_loop(self):
        """
        Se a simulação externa não enviar posição via MQTT, este loop move caminhões
        que estiverem em modo Automático localmente (comportamento 'vivo').
        """
        while True:
            time.sleep(0.25)
            if not self.trucks:
                continue
            for tid in self.trucks:
                mode_btn = self.mode_buttons.get(tid)
                if mode_btn and "Automático" in mode_btn.text:
                    data = self.truck_data.get(tid, {})
                    # se dados externos existem, não sobrescrever
                    if 'i_posicao_x' in data and 'i_posicao_y' in data:
                        continue
                    dx = random.uniform(-1.5, 1.5)
                    dy = random.uniform(-1.5, 1.5)
                    cx = float(data.get('i_posicao_x', self.COORD_MAX_X / 2))
                    cy = float(data.get('i_posicao_y', self.COORD_MAX_Y / 2))
                    nx = max(0, min(self.COORD_MAX_X, cx + dx))
                    ny = max(0, min(self.COORD_MAX_Y, cy + dy))
                    self.truck_data.setdefault(tid, {})
                    self.truck_data[tid]['i_posicao_x'] = nx
                    self.truck_data[tid]['i_posicao_y'] = ny
                    self.page.run_async(self.update_truck_data, tid)

    # ---------------- Limpeza / fechamento ----------------
    def on_close(self, e):
        # Fechar socket e mqtt com segurança
        try:
            if self.sock:
                self.sock.close()
                self.sock = None
        except:
            pass
        try:
            self.mqtt_client.loop_stop()
        except:
            pass
        print("Interface fechada.")

# ---------------- Entrypoint ----------------
def main(page: ft.Page):
    MQTTInterface(page)

if __name__ == "__main__":
    ft.app(target=main)
