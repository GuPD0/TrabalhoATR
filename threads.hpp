#ifndef THREADS_HPP
#define THREADS_HPP

#include <atomic>
#include "classes.hpp"

// Declarações das funções das threads
void TratamentoSensores(BufferCircular& buf);
void MonitoramentoDeFalhas(BufferCircular& buf);
void LogicaDeComando(BufferCircular& buf);

#endif
