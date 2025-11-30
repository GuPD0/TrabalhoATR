# interface_mina.py
import flet as ft
import socket
import time
import threading
import paho.mqtt.client as mqtt
import random
import traceback

class MQTTInterface:
    # Configuração do mapa / coordenação
    MAP_W = 760
    MAP_H = 390
    COORD_MAX_X = 100  # coordenadas lógicas X vão de 0..100
    COORD_MAX_Y = 100  # coordenadas lógicas Y vão de 0..100

    def __init__(self, page: ft.Page):
        self.pellets = []   # lista de (x, y, control)
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

        # Controle de movimento local por caminhão (novo)
        self.local_move_enabled = {}

        # Cliente MQTT
        self.mqtt_client = mqtt.Client()
        self.mqtt_client.on_message = self.on_mqtt_message
        try:
            self.mqtt_client.connect("localhost", 1883, 60)
            # Assinaturas iniciais
            self.mqtt_client.subscribe("truck/+/sensor/+")
            # Recebe posições calculadas pelo planejador
            self.mqtt_client.subscribe("planner/truck/+/position")
            self.mqtt_client.loop_start()
        except Exception as e:
            print("Erro ao conectar MQTT:", e)
            traceback.print_exc()

        # Componentes da UI (controle esquerdo)
        self.add_button = ft.ElevatedButton("Adicionar Caminhão", on_click=self.add_truck)
        self.close_button = ft.ElevatedButton("Fechar Interface", on_click=self.close_interface)
        self.reconnect_button = ft.ElevatedButton("Reconectar Socket", on_click=self.reconnect_socket)
        self.truck_count_label = ft.Text("N° de caminhões: 0", size=14, weight=ft.FontWeight.BOLD)
        self.error_label = ft.Text("", color=ft.Colors.RED, size=12)
        self.truck_list = ft.Column(scroll=ft.ScrollMode.ALWAYS, height=400, expand=True)

        # Dicionários por caminhão
        self.failure_buttons = {}
        self.mode_buttons = {}
        self.manual_inputs = {}
        self.data_labels = {}
        self.local_move_buttons = {}  # novo: botão para ativar/desativar movimento local

        # Criar mapa Pac-Man (estilo B: minimalista — paredes sem bolinhas)
        self.map_container = self.create_pacman_map()

        controls_column = ft.Container(
            width=1040,
            content=ft.Column(
                [
                    ft.Row([self.add_button, self.reconnect_button, self.close_button],
                        alignment=ft.MainAxisAlignment.SPACE_BETWEEN),
                    self.truck_count_label,
                    self.error_label,
                    ft.Text("Caminhões:", size=16, weight=ft.FontWeight.BOLD),
                    self.truck_list
                ],
                spacing=10
            )
        )

        self.page.add(
            ft.Container(
                content=ft.Column(
                    [
                        controls_column,
                        ft.Divider(height=20),
                        ft.Row(
                            controls=[
                                self.map_container
                            ],
                            alignment=ft.MainAxisAlignment.CENTER
                        )
                    ],
                    alignment=ft.MainAxisAlignment.START
                ),
                padding=10
            )
        )

        # on_close handler seguro
        self.page.on_close = self.on_close

        # configurar handler para mensagens enviadas por threads
        # (deve ser registrado uma vez, aqui no init)
        self.page.on_view_message = self.on_page_message

        # conectar socket e iniciar threads
        self.connect_socket()
        threading.Thread(target=self.auto_reconnect, daemon=True).start()
        threading.Thread(target=self.auto_move_loop, daemon=True).start()

    # ---------------- MQTT callbacks ----------------
    def on_mqtt_message(self, client, userdata, msg):
        try:
            topic = msg.topic
            payload = msg.payload.decode()
            parts = topic.split('/')
            # Tratamento de tópicos da forma 'truck/{id}/sensor/{sensor}'
            if len(parts) >= 4 and parts[0] == 'truck' and parts[2] == 'sensor':
                try:
                    truck_id = int(parts[1])
                except:
                    return
                sensor = parts[3]
                if truck_id not in self.truck_data:
                    self.truck_data[truck_id] = {}
                # marca que esses dados vieram do MQTT (externo)
                self.truck_data[truck_id]['_external'] = True
                self.truck_data[truck_id][sensor] = payload
                # Envia evento para thread principal (Flet 0.28+)
                try:
                    self.page.send({"type": "update_truck", "truck_id": truck_id})
                except Exception:
                    # fallback — se por algum motivo page.send falhar, tente atualizar via callback direto
                    try:
                        self.update_truck_data(truck_id)
                    except Exception:
                        pass
                return

            # -----------------------------------------------------------
            # NOVO: posição vinda do PLANEJADOR DE ROTA
            # tópico esperado: planner/truck/{id}/position
            # payload esperado: "x,y" (ex: "42.3,12.5")
            # -----------------------------------------------------------
            if len(parts) >= 4 and parts[0] == 'planner' and parts[1] == 'truck' and parts[3] == 'position':
                try:
                    truck_id = int(parts[2])
                except:
                    return

                try:
                    px_str, py_str = payload.split(',')
                    px = float(px_str)
                    py = float(py_str)
                except:
                    print("Payload inválido de position do planner:", payload)
                    return

                if truck_id not in self.truck_data:
                    self.truck_data[truck_id] = {}

                self.truck_data[truck_id]["i_posicao_x"] = px
                self.truck_data[truck_id]["i_posicao_y"] = py
                self.truck_data[truck_id]["_external"] = True

                # notifica UI para atualizar
                try:
                    self.page.send({"type": "update_truck", "truck_id": truck_id})
                except Exception:
                    self.update_truck_data(truck_id)
                return

        except Exception as e:
            print("Erro em on_mqtt_message:", e)
            traceback.print_exc()

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
            # atualização final da UI
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

        # inicializa dados do caminhão, marcando como não-external por padrão
        center_x, center_y = self.COORD_MAX_X/2, self.COORD_MAX_Y/2
        self.truck_data[truck_id] = {
            '_external': False,
            'i_posicao_x': center_x,
            'i_posicao_y': center_y
        }

        self.add_truck_to_gui(truck_id)
        # posição inicial central do mapa (lógicas 0..100)
        self.move_truck_on_map(truck_id, center_x, center_y)
        # habilitar movimento local por padrão como False
        self.local_move_enabled[truck_id] = False
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
        # verifica colisão com bolinhas já existentes (usa coordenadas em pixels ou lógicas conforme uso)
        for px, py, icon in self.pellets:
            px_p = getattr(icon, "left", px)
            py_p = getattr(icon, "top", py)
            if abs(px_p - x) < self.pellet_size*2 and abs(py_p - y) < self.pellet_size*2:
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
        temp_button = ft.ElevatedButton("Falha Temperatura", on_click=lambda e, tid=truck_id: self.inject_temp_failure(tid))
        electric_button = ft.ElevatedButton("Falha Elétrica", on_click=lambda e, tid=truck_id: self.inject_electric_failure(tid))
        hydraulic_button = ft.ElevatedButton("Falha Hidráulica", on_click=lambda e, tid=truck_id: self.inject_hydraulic_failure(tid))
        self.failure_buttons[truck_id] = [temp_button, electric_button, hydraulic_button]

        # Botão modo
        mode_button = ft.ElevatedButton(f"Modo: Automático - Caminhão {truck_id}", on_click=lambda e, tid=truck_id: self.toggle_mode(tid))
        self.mode_buttons[truck_id] = mode_button

        # Botão de movimento local (novo)
        move_button = ft.ElevatedButton("Ativar Movimento", on_click=lambda e, tid=truck_id: self.toggle_local_move(tid))
        self.local_move_buttons[truck_id] = move_button

        # Inputs manuais (invisíveis até ativar manual)
        x_input = ft.TextField(label="Posição X (int)", width=100, visible=False)
        y_input = ft.TextField(label="Posição Y (int)", width=100, visible=False)
        ang_input = ft.TextField(label="Ângulo (-180 a 180)", width=120, visible=False)
        set_manual_button = ft.ElevatedButton("Definir Manual", on_click=lambda e, tid=truck_id, xi=x_input, yi=y_input, ai=ang_input: self.set_manual(tid, xi, yi, ai), visible=False)
        self.manual_inputs[truck_id] = (x_input, y_input, ang_input, set_manual_button)

        # Label de dados
        data_label = ft.Text(f"Dados: X: N/A, Y: N/A, Ang: N/A", size=12)
        self.data_labels[truck_id] = data_label

        # Inputs do setpoint (destino)
        dest_x = ft.TextField(label="Destino X", width=80)
        dest_y = ft.TextField(label="Destino Y", width=80)
        send_sp = ft.ElevatedButton("Enviar destino",
                                    on_click=lambda e, tid=truck_id, dx=dest_x, dy=dest_y: self.send_setpoint(tid, dx.value, dy.value))

        # Adicionar controles à lista; incluí o botão de movimento ao lado do modo
        controls_row = ft.Row([
            mode_button,
            move_button,
            ft.Row([temp_button, electric_button, hydraulic_button], spacing=5)
        ], alignment=ft.MainAxisAlignment.SPACE_BETWEEN, expand=True)

        self.truck_list.controls.append(
            ft.Column([
                ft.Text(f"Caminhão {truck_id}", size=14),
                controls_row,
                data_label,
                # NOVO BLOCO: DESTINO
                ft.Text("Destino do Caminhão:", size=12, weight=ft.FontWeight.BOLD),
                ft.Row([dest_x, dest_y, send_sp], spacing=5),
                ft.Row([x_input, y_input, ang_input, set_manual_button])
            ])
        )

        # criar ícone no mapa (Container com top/left) - posiciona já no centro (não 0,0)
        init_px, init_py = self.logical_to_pixel(self.COORD_MAX_X/2, self.COORD_MAX_Y/2)
        truck_icon = ft.Container(
            content=ft.Text(str(truck_id), size=12, weight=ft.FontWeight.BOLD),
            width=22,
            height=22,
            alignment=ft.alignment.center,
            bgcolor=ft.Colors.YELLOW_700,
            border_radius=ft.border_radius.all(11),
            tooltip=f"Caminhão {truck_id}",
            top=init_py,
            left=init_px
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

    def toggle_local_move(self, truck_id):
        # alterna flag e atualiza texto do botão
        enabled = self.local_move_enabled.get(truck_id, False)
        enabled = not enabled
        self.local_move_enabled[truck_id] = enabled
        btn = self.local_move_buttons.get(truck_id)
        if btn:
            btn.text = "Desativar Movimento" if enabled else "Ativar Movimento"
        print(f"Movimento local para caminhão {truck_id} = {enabled}")
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
            # marcar como local (pois foi setado manualmente pela UI)
            self.truck_data.setdefault(truck_id, {})['_external'] = False
            self.truck_data[truck_id]['i_posicao_x'] = x
            self.truck_data[truck_id]['i_posicao_y'] = y
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
        wall_thickness = 12
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
            (60, 300, 200, 16),
            (500, 300, 200, 16),
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

        # --- verificar colisão entre caminhões ---
        for other_id, other_icon in self.map_truck_icons.items():
            if other_id == truck_id:
                continue
            ox = getattr(other_icon, "left", None)
            oy = getattr(other_icon, "top", None)
            if ox is None or oy is None:
                continue
            dist = ((px - ox)**2 + (py - oy)**2) ** 0.5
            if dist < 22:  # tamanho aproximado das bolinhas
                print(f"COLISÃO ENTRE CAMINHÕES: {truck_id} bateu em {other_id}")
                return  # cancela movimento

        # --- colisão com as paredes ---
        for wall in self.map_stack.controls:
            # pular se for o próprio ícone
            if wall == self.map_truck_icons[truck_id]:
                continue
            wall_color = getattr(wall, "bgcolor", None)
            if wall_color == ft.Colors.BLUE_900:  # identifica paredes
                wx = getattr(wall, "left", None)
                wy = getattr(wall, "top", None)
                ww = getattr(wall, "width", None)
                wh = getattr(wall, "height", None)
                if None in (wx, wy, ww, wh):
                    continue
                # checagem simples de colisão (caixa)
                if (px + 22 > wx and px < wx + ww and
                    py + 22 > wy and py < wy + wh):
                    print(f"COLISÃO COM PAREDE: Caminhão {truck_id}")
                    return  # impede mover

        icon = self.map_truck_icons[truck_id]
        icon.left = px
        icon.top = py

        # colisão com pellets: usamos a posição real do control (left/top)
        # se colidir, removemos o pellet (comportamento 'comer a bolinha')
        removed_any = False
        for (orig_x, orig_y, pellet) in list(self.pellets):
            p_left = getattr(pellet, "left", orig_x)
            p_top = getattr(pellet, "top", orig_y)
            if abs(px - p_left) < self.pellet_size and abs(py - p_top) < self.pellet_size:
                print(f"COLISÃO: Caminhão {truck_id} bateu em uma bolinha!")
                # remove do canvas e da lista
                try:
                    self.map_stack.controls.remove(pellet)
                except ValueError:
                    pass
                try:
                    self.pellets.remove((orig_x, orig_y, pellet))
                except ValueError:
                    # procurar e remover qualquer tupla com esse control
                    self.pellets = [t for t in self.pellets if t[2] != pellet]
                removed_any = True
                # continue — não return, deixamos o caminhão ser posicionado sobre o local

        if removed_any:
            # atualiza a UI mostrando a bolinha removida
            self.page.update()
        else:
            # atualização simples de posição
            self.page.update()

    # ---------------- Modo automático local ----------------
    def auto_move_loop(self):
        """
        Loop de movimento automático local.
        Atualiza caminhões apenas se:
          - movimento local estiver ativado;
          - caminhão estiver em modo Automático;
          - posição não vier de fonte externa (_external == False).
        Envia eventos via page.send() (correto para Flet 0.28.x).
        """
        while True:
            time.sleep(0.25)

            if not self.trucks:
                continue

            for tid in self.trucks:

                # só move se movimento local estiver ativado para este caminhão
                if not self.local_move_enabled.get(tid, False):
                    continue

                # só anda se o botão diz que está em modo Automático
                mode_btn = self.mode_buttons.get(tid)
                if not (mode_btn and "Automático" in mode_btn.text):
                    continue

                data = self.truck_data.get(tid, {})

                # se posição veio de MQTT (planner ou sensor), não sobrescrever
                if data.get('_external', False):
                    continue

                # ----------------------------
                # cálculo do movimento aleatório
                # ----------------------------
                dx = random.uniform(-1.5, 1.5)
                dy = random.uniform(-1.5, 1.5)

                cx = float(data.get("i_posicao_x", self.COORD_MAX_X / 2))
                cy = float(data.get("i_posicao_y", self.COORD_MAX_Y / 2))

                nx = max(0, min(self.COORD_MAX_X, cx + dx))
                ny = max(0, min(self.COORD_MAX_Y, cy + dy))

                # salva nova posição no buffer interno
                self.truck_data.setdefault(tid, {})
                self.truck_data[tid]["i_posicao_x"] = nx
                self.truck_data[tid]["i_posicao_y"] = ny
                self.truck_data[tid]["_external"] = False

                # notifica thread principal da UI
                try:
                    self.page.send({
                        "type": "update_truck",
                        "truck_id": tid
                    })
                except Exception:
                    # fallback seguro
                    try:
                        self.update_truck_data(tid)
                    except Exception:
                        pass

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

    # ---------------- Handler de mensagens da view (UI thread) ----------------
    def on_page_message(self, e):
        """
        Handler para mensagens enviadas por threads via page.send().
        Trata 'update_truck' e outros eventos se necessário.
        """
        # Em diferentes versões do Flet o payload pode vir em e.data ou e.message
        msg = None
        if hasattr(e, "data"):
            msg = e.data
        elif hasattr(e, "message"):
            msg = e.message
        elif hasattr(e, "args") and len(e.args) > 0:
            # fallback raro
            msg = e.args[0]

        if not msg:
            return

        try:
            if isinstance(msg, dict) and msg.get("type") == "update_truck":
                tid = msg.get("truck_id")
                if tid is not None:
                    self.update_truck_data(tid)
        except Exception as ex:
            print("Erro em on_page_message:", ex)

    # --------------------- MQTT: enviar setpoint ---------------------
    def send_setpoint(self, truck_id, x, y):
        try:
            x = float(x)
            y = float(y)
        except:
            self.error_label.value = f"Destino inválido para caminhão {truck_id}"
            self.page.update()
            return

        topic = f"planner/truck/{truck_id}/setpoint"
        payload = f"{x},{y}"

        try:
            self.mqtt_client.publish(topic, payload)
            print(f"[MQTT] Setpoint enviado p/ caminhão {truck_id}: ({x},{y})")
            self.error_label.value = f"Destino enviado para caminhão {truck_id}"
        except Exception as e:
            print("Erro ao publicar setpoint via MQTT:", e)
            self.error_label.value = f"Erro ao enviar destino para caminhão {truck_id}"
        self.page.update()

# ---------------- Entrypoint ----------------
def main(page: ft.Page):
    MQTTInterface(page)

if __name__ == "__main__":
    ft.app(target=main)
