#ifndef CONTROLE_NAVEGACAO_HPP
#define CONTROLE_NAVEGACAO_HPP

#include "classes.hpp"

// Thread da tarefa Controle de Navegação
// Lê do BufferCircular e publica comandos de atuadores no buffer.
// Recebe notificações de falhas via variáveis atômicas externas (events).
void ControleDeNavegacao(BufferCircular& buf);

#endif // CONTROLE_NAVEGACAO_HPP
