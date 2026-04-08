#include "jogo.h"
#include "hardware.h"
#include "audio.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include <stdlib.h>
#include <time.h>

#define DEBOUNCE_TIME_MS 200
#define NUM_BOTOES 4

volatile int flg_inicio = 0;
volatile int flg_botao[NUM_BOTOES] = {0};
volatile uint32_t ultimo_tempo_botao[NUM_BOTOES] = {0};
JogoState* ptr_jogo_irq = NULL;

static inline int identificar_botao(uint gpio) {
    if (gpio == btn_r) return 0;
    if (gpio == btn_g) return 1;
    if (gpio == btn_b) return 2;
    if (gpio == btn_y) return 3;
    return -1;
}

void btn_callback(uint gpio, uint32_t events) {
    if (events == GPIO_IRQ_EDGE_FALL) {
        if (ptr_jogo_irq == NULL) return;
        
        if (!(*ptr_jogo_irq).rodando) {
            flg_inicio = 1;
            return;
        }
        if ((*ptr_jogo_irq).estado_atual == ESTADO_AGUARDANDO_JOGADA && !(*ptr_jogo_irq).btn_p) {
            uint32_t tempo_atual = to_ms_since_boot(get_absolute_time());
            int botao_id = identificar_botao(gpio);
            if (botao_id >= 0 && (tempo_atual - ultimo_tempo_botao[botao_id] > DEBOUNCE_TIME_MS)) {
                ultimo_tempo_botao[botao_id] = tempo_atual;
                flg_botao[botao_id] = 1;
            }
        }
    }
}

void botoes_init_com_irq(void) {
    for(int i = 0; i < NUM_BOTOES; i++) {
        gpio_init(botoes_pin[i]);
        gpio_set_dir(botoes_pin[i], GPIO_IN);
        gpio_pull_up(botoes_pin[i]);
        gpio_set_irq_enabled_with_callback(botoes_pin[i], GPIO_IRQ_EDGE_FALL, true, &btn_callback);
    }
}

int64_t timeout_callback(alarm_id_t id, void *user_data) {
    JogoState* jogo = (JogoState*)user_data;
    if ((*jogo).estado_atual == ESTADO_AGUARDANDO_JOGADA) {
        (*jogo).flg_timeout = 1;
    }
    return 0;
}

void iniciar_timer(JogoState* jogo) {
    if ((*jogo).id_alarme != -1) {
        cancel_alarm((*jogo).id_alarme);
    }
    (*jogo).id_alarme = add_alarm_in_ms(TEMPO_LIMITE_MS, timeout_callback, jogo, false);
}

void cancelar_timer(JogoState* jogo) {
    if ((*jogo).id_alarme != -1) {
        cancel_alarm((*jogo).id_alarme);
        (*jogo).id_alarme = -1;
    }
}

void transicao_estado(JogoState* jogo, EstadoJogo novo_estado) {
    (*jogo).estado_atual = novo_estado;
}

void jogo_init(JogoState* jogo) {
    (*jogo).tamanho = 0;
    (*jogo).rodando = 0;
    (*jogo).aguardando_jogada = 0;
    (*jogo).btn_p = 0;
    (*jogo).flg_timeout = 0;
    (*jogo).id_alarme = -1;
    (*jogo).estado_atual = ESTADO_AGUARDANDO_INICIO;
    (*jogo).indice_reproducao = 0;
    (*jogo).n_jogada = 0;
    (*jogo).btn_pressionado = 0;
    (*jogo).animacao_contador = 0;
    (*jogo).tempo_ultima_jogada = 0;
    
    for(int i = 0; i < MAX_SEQUENCIA; i++) {
        (*jogo).sequencia[i] = 0;
    }
}

static void processar_estado_aguardando_inicio(JogoState* jogo) {
    if (flg_inicio) {
        flg_inicio = 0;
        srand(time(NULL));
        (*jogo).tamanho = 0;
        (*jogo).rodando = 1;
        (*jogo).btn_p = 0;
        (*jogo).flg_timeout = 0;
        (*jogo).n_jogada = 0;
        (*jogo).btn_pressionado = 0;
        (*jogo).tempo_ultima_jogada = 0;
        
        for(int i = 0; i < NUM_BOTOES; i++) {
            flg_botao[i] = 0;
            ultimo_tempo_botao[i] = 0;
        }
        
        limpar_pontuacao();
        tocar_musica_fundo();
        
        (*jogo).sequencia[(*jogo).tamanho] = rand() % 4;
        (*jogo).tamanho++;
        (*jogo).indice_reproducao = 0;
        
        transicao_estado(jogo, ESTADO_REPRODUZINDO_SEQUENCIA);
    }
}

static void processar_estado_reproduzindo_sequencia(JogoState* jogo) {
    if ((*jogo).indice_reproducao < (*jogo).tamanho) {
        int cor = (*jogo).sequencia[(*jogo).indice_reproducao];
        acender_led(cor, 300);
        tocar_som_botao(cor);
        sleep_ms(200);
        (*jogo).indice_reproducao++;
    } else {
        (*jogo).aguardando_jogada = 1;
        (*jogo).btn_p = 0;
        (*jogo).n_jogada = 0;
        
        iniciar_timer(jogo);
        
        for(int i = 0; i < 4; i++) {
            flg_botao[i] = 0;
        }
        
        transicao_estado(jogo, ESTADO_AGUARDANDO_JOGADA);
    }
}

static void processar_estado_aguardando_jogada(JogoState* jogo) {
    if ((*jogo).flg_timeout) {
        (*jogo).flg_timeout = 0;
        transicao_estado(jogo, ESTADO_DERROTA);
    } else {
        for(int i = 0; i < 4; i++) {
            if (flg_botao[i]) {
                (*jogo).btn_p = 1;
                (*jogo).btn_pressionado = i;
                (*jogo).tempo_ultima_jogada = to_ms_since_boot(get_absolute_time());
                flg_botao[i] = 0;
                
                for(int j = 0; j < 4; j++) {
                    if (j != i) flg_botao[j] = 0;
                }
                
                transicao_estado(jogo, ESTADO_PROCESSANDO_JOGADA);
                break;
            }
        }
    }
}

static void processar_estado_processando_jogada(JogoState* jogo) {
    int i = (*jogo).btn_pressionado;
    acender_led(i, 150);
    tocar_som_botao(i);
    sleep_ms(50);
    
    if(i == (*jogo).sequencia[(*jogo).n_jogada]) {
        (*jogo).n_jogada++;
        
        if((*jogo).n_jogada >= (*jogo).tamanho) {
            atualizar_pontuacao((*jogo).tamanho);
            
            if ((*jogo).tamanho >= 10) {
                transicao_estado(jogo, ESTADO_VITORIA);
            } else {
                (*jogo).sequencia[(*jogo).tamanho] = rand() % 4;
                (*jogo).tamanho++;
                (*jogo).indice_reproducao = 0;
                (*jogo).n_jogada = 0;
                (*jogo).aguardando_jogada = 0;
                (*jogo).btn_p = 0;
                sleep_ms(500);
                
                for(int j = 0; j < 4; j++) {
                    flg_botao[j] = 0;
                }
                
                transicao_estado(jogo, ESTADO_REPRODUZINDO_SEQUENCIA);
            }
        } else {
            transicao_estado(jogo, ESTADO_AGUARDANDO_PROXIMA);
        }
    } else {
        transicao_estado(jogo, ESTADO_DERROTA);
    }
}

static void processar_estado_aguardando_proxima(JogoState* jogo) {
    sleep_ms(150);
    
    for(int i = 0; i < 4; i++) {
        flg_botao[i] = 0;
    }
    
    iniciar_timer(jogo);
    (*jogo).btn_p = 0;
    transicao_estado(jogo, ESTADO_AGUARDANDO_JOGADA);
}

void executar_animacao_vitoria(JogoState* jogo) {
    (*jogo).rodando = 0;
    (*jogo).aguardando_jogada = 0;
    (*jogo).btn_p = 0;
    cancelar_timer(jogo);
    
    if ((*jogo).animacao_contador < 3) {
        for(int i = 0; i < 4; i++) gpio_put(leds_pin[i], 1);
        sleep_ms(200);
        for(int i = 0; i < 4; i++) gpio_put(leds_pin[i], 0);
        sleep_ms(200);
        (*jogo).animacao_contador++;
    } else {
        tocar_som_vitoria();
        (*jogo).animacao_contador = 0;
        transicao_estado(jogo, ESTADO_AGUARDANDO_INICIO);
    }
}

void executar_animacao_derrota(JogoState* jogo) {
    (*jogo).rodando = 0;
    (*jogo).aguardando_jogada = 0;
    (*jogo).btn_p = 0;
    cancelar_timer(jogo);
    limpar_pontuacao();
    
    if ((*jogo).animacao_contador < 3) {
        for(int i = 0; i < 4; i++) gpio_put(leds_pin[i], 1);
        sleep_ms(200);
        for(int i = 0; i < 4; i++) gpio_put(leds_pin[i], 0);
        sleep_ms(200);
        (*jogo).animacao_contador++;
    } else {
        tocar_som_derrota();
        (*jogo).animacao_contador = 0;
        
        for(int i = 0; i < 4; i++) {
            flg_botao[i] = 0;
        }
        
        transicao_estado(jogo, ESTADO_AGUARDANDO_INICIO);
    }
}

void jogo_processar(JogoState* jogo) {
    EstadoJogo estado = (*jogo).estado_atual;
    
    if (estado == ESTADO_AGUARDANDO_INICIO) {
        processar_estado_aguardando_inicio(jogo);
    } else if (estado == ESTADO_REPRODUZINDO_SEQUENCIA) {
        processar_estado_reproduzindo_sequencia(jogo);
    } else if (estado == ESTADO_AGUARDANDO_JOGADA) {
        processar_estado_aguardando_jogada(jogo);
    } else if (estado == ESTADO_PROCESSANDO_JOGADA) {
        processar_estado_processando_jogada(jogo);
    } else if (estado == ESTADO_AGUARDANDO_PROXIMA) {
        processar_estado_aguardando_proxima(jogo);
    } else if (estado == ESTADO_VITORIA) {
        executar_animacao_vitoria(jogo);
    } else if (estado == ESTADO_DERROTA) {
        executar_animacao_derrota(jogo);
    }
}