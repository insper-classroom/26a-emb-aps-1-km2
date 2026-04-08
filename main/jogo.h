#ifndef JOGO_H
#define JOGO_H

#include "estados.h"
#include "pico/stdlib.h"

#define MAX_SEQUENCIA 20
#define TEMPO_LIMITE_MS 3000

typedef struct {
    int sequencia[MAX_SEQUENCIA];
    int tamanho;
    volatile int rodando;
    volatile int aguardando_jogada;
    volatile int btn_p;
    volatile int flg_timeout;
    alarm_id_t id_alarme;
    EstadoJogo estado_atual;
    int indice_reproducao;
    int n_jogada;
    int btn_pressionado;
    int animacao_contador;
    uint32_t tempo_ultima_jogada;
} JogoState;


extern volatile int flg_inicio;
extern volatile int flg_botao[4];
extern volatile uint32_t ultimo_tempo_botao[4];
extern JogoState* ptr_jogo_irq;


void jogo_init(JogoState* jogo);
void jogo_processar(JogoState* jogo);
void botoes_init_com_irq(void);
void iniciar_timer(JogoState* jogo);
void cancelar_timer(JogoState* jogo);
void transicao_estado(JogoState* jogo, EstadoJogo novo_estado);
void executar_animacao_vitoria(JogoState* jogo);
void executar_animacao_derrota(JogoState* jogo);

#endif