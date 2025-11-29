#ifndef TRATAMENTO_SENSORES_HPP
#define TRATAMENTO_SENSORES_HPP

#include "classes.hpp"
#include <atomic>

// Flag global de execução
extern std::atomic<bool> running;

// Thread de Tratamento de Sensores
void TratamentoSensores(BufferCircular& buf);

#endif
