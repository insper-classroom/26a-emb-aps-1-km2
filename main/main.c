#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include <stdlib.h>
#include <time.h>
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"

#include "win.h"
#include "lose.h"
#include "music.h"

// ================= PINOS =================
#define btn_r 15
#define btn_g 14
#define btn_b 12
#define btn_y 13

#define led_r 18
#define led_g 22
#define led_b 21
#define led_y 20

#define led_1 2
#define led_2 3
#define led_3 4
#define led_4 5
#define led_5 6
#define led_6 7
#define led_7 8
#define led_8 9
#define led_9 10
#define led_10 11

#define buzzer 28
#define AUDIO_PIN 16

#define DEBOUNCE_TIME_MS 200
#define VOLUME_PCT 5  
#define TEMPO_LIMITE_MS 3000
#define MAX_SEQUENCIA 20
#define NUM_BOTOES 4
#define NUM_PONTOS 10

// ================= ESTADOS DO JOGO =================
typedef enum {
    ESTADO_AGUARDANDO_INICIO,
    ESTADO_REPRODUZINDO_SEQUENCIA,
    ESTADO_AGUARDANDO_JOGADA,
    ESTADO_PROCESSANDO_JOGADA,
    ESTADO_AGUARDANDO_PROXIMA,
    ESTADO_VITORIA,
    ESTADO_DERROTA
} EstadoJogo;

// ================= ARRAYS =================
static const int pontos[NUM_PONTOS] = { led_10, led_9, led_8, led_7, led_6, led_5, led_4, led_3, led_2, led_1};
static const int botoes_pin[NUM_BOTOES] = {btn_r, btn_g, btn_b, btn_y};
static const int leds_pin[NUM_BOTOES] = {led_r, led_g, led_b, led_y};

// ================= ESTRUTURA DO JOGO =================
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

// ================= ESTRUTURA DE ÁUDIO =================
typedef struct {
    volatile const uint8_t* som_atual;
    volatile uint32_t tamanho_atual;
    volatile uint32_t wav_position;
    volatile bool tocando;
    volatile bool audio_loop;
} AudioState;

// ================= VARIÁVEIS PARA IRQ (necessárias) =================
static AudioState g_audio = {0};
volatile int flg_inicio = 0;
volatile int flg_botao[NUM_BOTOES] = {0};
volatile uint32_t ultimo_tempo_botao[NUM_BOTOES] = {0};
static JogoState* ptr_jogo_irq = NULL;

// ================= PROTÓTIPOS =================
void core1_entry(void);
void btn_callback(uint gpio, uint32_t events);
void botoes_init(void);
void atualizar_pontuacao(JogoState* jogo);
void limpar_pontuacao(void);
void acender_led(int cor, int tempo_ms);
void tocar_som_buzzer(int cor);
void iniciar_timer(JogoState* jogo);
void cancelar_timer(JogoState* jogo);
void maquina_estados(JogoState* jogo);
void transicao_estado(JogoState* jogo, EstadoJogo novo_estado);

// ================= TIMER =================
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

// ================= PWM IRQ =================
void pwm_interrupt_handler(void) {
    pwm_clear_irq(pwm_gpio_to_slice_num(AUDIO_PIN));

    if (g_audio.som_atual != NULL) {
        if (g_audio.wav_position < (g_audio.tamanho_atual << 3)) {
            uint8_t sample = g_audio.som_atual[g_audio.wav_position >> 3];
            int centered = (int)sample - 128;
            centered = (centered * VOLUME_PCT) / 100;
            int output = centered + 128;
            if (output < 0) output = 0;
            if (output > 255) output = 255;
            pwm_set_gpio_level(AUDIO_PIN, output);
            g_audio.wav_position++;
        } else if (g_audio.audio_loop) {
            g_audio.wav_position = 0;
        } else {
            irq_set_enabled(PWM_IRQ_WRAP, false);
            pwm_set_gpio_level(AUDIO_PIN, 0);
            g_audio.tocando = false;
            g_audio.som_atual = NULL;
        }
    }
}

void tocar_audio(const uint8_t* som, uint32_t tamanho, bool loop) {
    irq_set_enabled(PWM_IRQ_WRAP, false);
    g_audio.som_atual = som;
    g_audio.tamanho_atual = tamanho;
    g_audio.wav_position = 0;
    g_audio.audio_loop = loop;
    g_audio.tocando = true;
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

void init_audio(void) {
    set_sys_clock_khz(176000, true);
    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);
    int slice = pwm_gpio_to_slice_num(AUDIO_PIN);
    pwm_clear_irq(slice);
    pwm_set_irq_enabled(slice, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_interrupt_handler);
    irq_set_enabled(PWM_IRQ_WRAP, false);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 8.0f);
    pwm_config_set_wrap(&config, 250);
    pwm_init(slice, &config, true);
}

// ================= CORE 1 =================
void core1_entry(void) {
    init_audio();
    while (1) {
        uint32_t cmd = multicore_fifo_pop_blocking();
        if (cmd == 1) {
            tocar_audio(WAV_DATA_MUSIC, WAV_DATA_LENGTH_MUSIC, true);
        } else if (cmd == 2) {
            tocar_audio(WAV_DATA_WIN, WAV_DATA_LENGTH_WIN, false);
        } else if (cmd == 3) {
            tocar_audio(WAV_DATA_LOSE, WAV_DATA_LENGTH_LOSE, false);
        } else if (cmd >= 10 && cmd <= 13) {
            tocar_som_buzzer(cmd - 10);
        }
    }
}

// ================= HARDWARE =================
void botoes_init(void) {
    for(int i = 0; i < NUM_PONTOS; i++) {
        gpio_init(pontos[i]);
        gpio_set_dir(pontos[i], GPIO_OUT);
        gpio_put(pontos[i], 0);
    }
    for(int i = 0; i < NUM_BOTOES; i++) {
        gpio_init(botoes_pin[i]);
        gpio_set_dir(botoes_pin[i], GPIO_IN);
        gpio_pull_up(botoes_pin[i]);
        gpio_set_irq_enabled_with_callback(botoes_pin[i], GPIO_IRQ_EDGE_FALL, true, &btn_callback);
        gpio_init(leds_pin[i]);
        gpio_set_dir(leds_pin[i], GPIO_OUT);
    }
    gpio_init(buzzer);
    gpio_set_dir(buzzer, GPIO_OUT);
}

// ================= JOGO =================
void limpar_pontuacao(void) {
    for(int i = 0; i < NUM_PONTOS; i++) {
        gpio_put(pontos[i], 0);
    }
}

void atualizar_pontuacao(JogoState* jogo) {
    if ((*jogo).tamanho > 0 && (*jogo).tamanho <= NUM_PONTOS) {
        gpio_put(pontos[(*jogo).tamanho - 1], 1);
    }
}

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

void acender_led(int cor, int tempo_ms) {
    gpio_put(leds_pin[cor], 1);
    sleep_ms(tempo_ms);
    gpio_put(leds_pin[cor], 0);
}

void tocar_som_buzzer(int cor) {
    const int frequencias[4] = {262, 294, 330, 349};
    for(int i = 0; i < 50; i++) {
        gpio_put(buzzer, 1);
        sleep_us(500000 / frequencias[cor]);
        gpio_put(buzzer, 0);
        sleep_us(500000 / frequencias[cor]);
    }
}

void transicao_estado(JogoState* jogo, EstadoJogo novo_estado) {
    (*jogo).estado_atual = novo_estado;
}

void maquina_estados(JogoState* jogo) {
    int i;
    EstadoJogo estado = (*jogo).estado_atual;
    
    // ESTADO_AGUARDANDO_INICIO
    if (estado == ESTADO_AGUARDANDO_INICIO) {
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
            
            for(i = 0; i < NUM_BOTOES; i++) {
                flg_botao[i] = 0;
                ultimo_tempo_botao[i] = 0;
            }
            
            limpar_pontuacao();
            multicore_fifo_push_blocking(1);
            
            (*jogo).sequencia[(*jogo).tamanho] = rand() % 4;
            (*jogo).tamanho++;
            (*jogo).indice_reproducao = 0;
            
            transicao_estado(jogo, ESTADO_REPRODUZINDO_SEQUENCIA);
        }
    }
    // ESTADO_REPRODUZINDO_SEQUENCIA
    else if (estado == ESTADO_REPRODUZINDO_SEQUENCIA) {
        if ((*jogo).indice_reproducao < (*jogo).tamanho) {
            int cor = (*jogo).sequencia[(*jogo).indice_reproducao];
            acender_led(cor, 300);
            multicore_fifo_push_blocking(10 + cor);
            sleep_ms(200);
            (*jogo).indice_reproducao++;
        } else {
            (*jogo).aguardando_jogada = 1;
            (*jogo).btn_p = 0;
            (*jogo).n_jogada = 0;
            
            iniciar_timer(jogo);
            
            for(i = 0; i < 4; i++) {
                flg_botao[i] = 0;
            }
            
            transicao_estado(jogo, ESTADO_AGUARDANDO_JOGADA);
        }
    }
    // ESTADO_AGUARDANDO_JOGADA
    else if (estado == ESTADO_AGUARDANDO_JOGADA) {
        if ((*jogo).flg_timeout) {
            (*jogo).flg_timeout = 0;
            transicao_estado(jogo, ESTADO_DERROTA);
        } else {
            for(i = 0; i < 4; i++) {
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
    // ESTADO_PROCESSANDO_JOGADA
    else if (estado == ESTADO_PROCESSANDO_JOGADA) {
        i = (*jogo).btn_pressionado;
        acender_led(i, 150);
        multicore_fifo_push_blocking(10 + i);
        sleep_ms(50);
        
        if(i == (*jogo).sequencia[(*jogo).n_jogada]) {
            (*jogo).n_jogada++;
            
            if((*jogo).n_jogada >= (*jogo).tamanho) {
                atualizar_pontuacao(jogo);
                
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
    // ESTADO_AGUARDANDO_PROXIMA
    else if (estado == ESTADO_AGUARDANDO_PROXIMA) {
        sleep_ms(150);
        
        for(i = 0; i < 4; i++) {
            flg_botao[i] = 0;
        }
        
        iniciar_timer(jogo);
        (*jogo).btn_p = 0;
        transicao_estado(jogo, ESTADO_AGUARDANDO_JOGADA);
    }
    // ESTADO_VITORIA
    else if (estado == ESTADO_VITORIA) {
        (*jogo).rodando = 0;
        (*jogo).aguardando_jogada = 0;
        (*jogo).btn_p = 0;
        cancelar_timer(jogo);
        
        if ((*jogo).animacao_contador < 3) {
            for(i = 0; i < 4; i++) gpio_put(leds_pin[i], 1);
            sleep_ms(200);
            for(i = 0; i < 4; i++) gpio_put(leds_pin[i], 0);
            sleep_ms(200);
            (*jogo).animacao_contador++;
        } else {
            multicore_fifo_push_blocking(2);
            (*jogo).animacao_contador = 0;
            transicao_estado(jogo, ESTADO_AGUARDANDO_INICIO);
        }
    }
    // ESTADO_DERROTA
    else if (estado == ESTADO_DERROTA) {
        (*jogo).rodando = 0;
        (*jogo).aguardando_jogada = 0;
        (*jogo).btn_p = 0;
        cancelar_timer(jogo);
        limpar_pontuacao();
        
        if ((*jogo).animacao_contador < 3) {
            for(i = 0; i < 4; i++) gpio_put(leds_pin[i], 1);
            sleep_ms(200);
            for(i = 0; i < 4; i++) gpio_put(leds_pin[i], 0);
            sleep_ms(200);
            (*jogo).animacao_contador++;
        } else {
            multicore_fifo_push_blocking(3);
            (*jogo).animacao_contador = 0;
            
            for(i = 0; i < 4; i++) {
                flg_botao[i] = 0;
            }
            
            transicao_estado(jogo, ESTADO_AGUARDANDO_INICIO);
        }
    }
}

// ================= MAIN =================
int main(void) {
    JogoState jogo;
    
    jogo.tamanho = 0;
    jogo.rodando = 0;
    jogo.aguardando_jogada = 0;
    jogo.btn_p = 0;
    jogo.flg_timeout = 0;
    jogo.id_alarme = -1;
    jogo.estado_atual = ESTADO_AGUARDANDO_INICIO;
    jogo.indice_reproducao = 0;
    jogo.n_jogada = 0;
    jogo.btn_pressionado = 0;
    jogo.animacao_contador = 0;
    jogo.tempo_ultima_jogada = 0;
    
    for(int i = 0; i < MAX_SEQUENCIA; i++) {
        jogo.sequencia[i] = 0;
    }
    
    ptr_jogo_irq = &jogo;
    
    stdio_init_all();
    botoes_init();
    multicore_launch_core1(core1_entry);

    while (true) {
        maquina_estados(&jogo);
        sleep_ms(10);
    }

    return 0;
}