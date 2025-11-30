FROM debian:trixie

# ---------------------------------------------------------
# 1. Atualiza sistema e instala dependências
# ---------------------------------------------------------
RUN apt update && apt install -y \
    g++ \
    make \
    libmosquitto-dev \
    python3 \
    python3-pip \
    python3-venv \
    && apt clean

# ---------------------------------------------------------
# 2. Instala dependências Python (Flet + Paho MQTT)
#    Usando --break-system-packages para evitar erro do PEP668
# ---------------------------------------------------------
RUN pip3 install --break-system-packages flet paho-mqtt

# ---------------------------------------------------------
# 3. Copia o projeto para o container
# ---------------------------------------------------------
WORKDIR /app
COPY . /app

# ---------------------------------------------------------
# 4. Compila o projeto C++ (Makefile)
# ---------------------------------------------------------
RUN make

# ---------------------------------------------------------
# 5. Comando padrão (ajuste se quiser rodar outro binário)
# ---------------------------------------------------------
CMD ["./caminhao"]
