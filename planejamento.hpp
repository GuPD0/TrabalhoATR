#ifndef PLANEJAMENTO_HPP
#define PLANEJAMENTO_HPP

#include "classes.hpp"

// Função principal da thread de Planejamento de Rota
// Responsabilidade: Ler Missao (Destino) + Ultima Posição -> Calcular Vetor -> Enviar Setpoint (Buffer)
void PlanejamentoDeRota(BufferCircular& buf);

#endif // PLANEJAMENTO_HPP