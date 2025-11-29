#ifndef COLETOR_DE_DADOS_HPP
#define COLETOR_DE_DADOS_HPP

#include "classes.hpp"
#include <atomic>

extern std::atomic<bool> running;

// thread principal do coletor
void ColetorDeDados(BufferCircular& buf);

#endif
