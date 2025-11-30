# -------------------------------------------------------------
# Makefile - Projeto ATR (Caminhão, Mina, Interface Local)
# -------------------------------------------------------------

CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2
LIBS = -lmosquitto -lpthread

# -------------------------------------------------------------
# ARQUIVOS DO CAMINHÃO (processo principal)
# -------------------------------------------------------------

CAMINHAO_SRCS = \
    main.cpp \
    mqtt.cpp \
    TratamentoSensores.cpp \
    controle_navegacao.cpp \
    ColetorDeDados.cpp \
    LogicaDeComando.cpp \
    MonitoramentoDeFalhas.cpp \
    planejamento.cpp

CAMINHAO_OBJS = $(CAMINHAO_SRCS:.cpp=.o)
CAMINHAO_BIN = caminhao

# -------------------------------------------------------------
# ARQUIVOS DA SIMULAÇÃO DA MINA
# -------------------------------------------------------------

MINA_SRCS = SimulacaoDaMina.cpp
MINA_OBJS = $(MINA_SRCS:.cpp=.o)
MINA_BIN = mina

# -------------------------------------------------------------
# ARQUIVOS DA INTERFACE LOCAL (processo separado)
# -------------------------------------------------------------

INTERFACE_SRCS = InterfaceLocal.cpp
INTERFACE_OBJS = $(INTERFACE_SRCS:.cpp=.o)
INTERFACE_BIN = interface

# -------------------------------------------------------------
# REGRAS GERAIS
# -------------------------------------------------------------

all: $(CAMINHAO_BIN) $(MINA_BIN) $(INTERFACE_BIN)

# -------------------------------------------------------------
# Construção do CAMINHÃO
# -------------------------------------------------------------

$(CAMINHAO_BIN): $(CAMINHAO_OBJS)
	$(CXX) $(CXXFLAGS) $(CAMINHAO_OBJS) $(LIBS) -o $(CAMINHAO_BIN)
	@echo ">> Caminhão compilado com sucesso."

# -------------------------------------------------------------
# Construção da MINA
# -------------------------------------------------------------

$(MINA_BIN): $(MINA_OBJS)
	$(CXX) $(CXXFLAGS) $(MINA_OBJS) $(LIBS) -o $(MINA_BIN)
	@echo ">> Simulação da Mina compilada com sucesso."

# -------------------------------------------------------------
# Construção da INTERFACE LOCAL
# -------------------------------------------------------------

$(INTERFACE_BIN): $(INTERFACE_OBJS)
	$(CXX) $(CXXFLAGS) $(INTERFACE_OBJS) $(LIBS) -o $(INTERFACE_BIN)
	@echo ">> Interface Local compilada com sucesso."

# -------------------------------------------------------------
# Regras de compilação
# -------------------------------------------------------------

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# -------------------------------------------------------------
# Limpeza
# -------------------------------------------------------------

clean:
	rm -f *.o $(CAMINHAO_BIN) $(MINA_BIN) $(INTERFACE_BIN)
	@echo ">> Limpeza completa."

# -------------------------------------------------------------
# Execuções rápidas
# -------------------------------------------------------------

run-caminhao: $(CAMINHAO_BIN)
	./$(CAMINHAO_BIN)

run-mina: $(MINA_BIN)
	./$(MINA_BIN)

run-interface: $(INTERFACE_BIN)
	./$(INTERFACE_BIN)

# -------------------------------------------------------------
# Fim do Makefile
# -------------------------------------------------------------
