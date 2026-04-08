#include <stdio.h>
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

#define DEBOUNCE_TIME_MS 100
#define VOLUME_PCT 5  

// 🔥 TIMER
#define TEMPO_LIMITE_MS 3000

// ================= ARRAYS =================
const int pontos[10] = { led_10, led_9, led_8, led_7, led_6, led_5, led_4, led_3, led_2, led_1};
const int botoes_pin[4] = {btn_r, btn_g, btn_b, btn_y};
const int leds_pin[4] = {led_r, led_g, led_b, led_y};

// ================= ESTRUTURA PARA DADOS DO JOGO =================
typedef struct {
    int sequencia[20];
    int tamanho;
    int flg_rodando;
    int aguardando_jogada;
    int btn_p;
    alarm_id_t id_alarme;
    int n_jogada;
    int btn_c;
} JogoState;

// Instância única (necessária para o programa)
static JogoState jogo = { .tamanho = 0, .flg_rodando = 0, .aguardando_jogada = 0, 
                          .btn_p = 0, .id_alarme = -1, .n_jogada = 0, .btn_c = 0 };

// ================= VARIÁVEIS PARA COMUNICAÇÃO COM IRQ =================
// Usamos variáveis estáticas com volatile para IRQ
static volatile int flg_inicio = 0;
static volatile int flg_botao_pressionado = -1;  // -1 = nenhum, 0-3 = botão
static volatile uint32_t ultimo_tempo_botao[4] = {0, 0, 0, 0};

// ================= ÁUDIO (acessado pela IRQ do PWM) =================
static volatile const uint8_t* som_atual = NULL;
static volatile uint32_t tamanho_atual = 0;
static volatile uint32_t wav_position = 0;
static volatile bool tocando = false;
static volatile bool audio_loop = false;

// ================= PROTÓTIPOS =================
void core1_entry();
void btn_callback(uint gpio, uint32_t events);
void botoes(void);
void new_game(void);
void next_level(void);
void perdeu(void);
void jogada(void);
void atualizar_pontuacao(void);
void limpar_pontuacao(void);
void acender_led(int cor, int tempo_ms);
void tocar_som(int cor);

// 🔥 TIMER
void iniciar_timer();
int64_t timeout_callback(alarm_id_t id, void *user_data);

// ================= TIMER =================
int64_t timeout_callback(alarm_id_t id, void *user_data) {
    if (jogo.aguardando_jogada) {
        jogo.flg_rodando = 0;
    }
    return 0;
}

void iniciar_timer() {
    if (jogo.id_alarme != -1) {
        cancel_alarm(jogo.id_alarme);
    }
    jogo.id_alarme = add_alarm_in_ms(TEMPO_LIMITE_MS, timeout_callback, NULL, false);
}

// ================= PWM IRQ =================
void pwm_interrupt_handler() {
    pwm_clear_irq(pwm_gpio_to_slice_num(AUDIO_PIN));

    if (som_atual != NULL) {
        if (wav_position < (tamanho_atual << 3)) {
            uint8_t sample = som_atual[wav_position >> 3];

            int centered = (int)sample - 128;
            centered = (centered * VOLUME_PCT) / 100;
            int output = centered + 128;

            if (output < 0) output = 0;
            if (output > 255) output = 255;

            pwm_set_gpio_level(AUDIO_PIN, output);
            wav_position++;
        } else if (audio_loop) {
            wav_position = 0;
        } else {
            irq_set_enabled(PWM_IRQ_WRAP, false);
            pwm_set_gpio_level(AUDIO_PIN, 0);
            tocando = false;
            som_atual = NULL;
        }
    }
}

void tocar_audio(const uint8_t* som, uint32_t tamanho, bool loop) {
    irq_set_enabled(PWM_IRQ_WRAP, false);

    som_atual = som;
    tamanho_atual = tamanho;
    wav_position = 0;
    audio_loop = loop;

    tocando = true;
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

void init_audio() {
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
void core1_entry() {
    init_audio();

    while (1) {
        uint32_t cmd = multicore_fifo_pop_blocking();

        if (cmd == 1) {
            tocar_audio(WAV_DATA_MUSIC, WAV_DATA_LENGTH_MUSIC, true);
        }
        else if (cmd == 2) {
            tocar_audio(WAV_DATA_WIN, WAV_DATA_LENGTH_WIN, false);
        }
        else if (cmd == 3) {
            tocar_audio(WAV_DATA_LOSE, WAV_DATA_LENGTH_LOSE, false);
        }
        else if (cmd >= 10 && cmd <= 13) {
            tocar_som(cmd - 10);
        }
    }
}

// ================= HARDWARE =================
void botoes() {
    for(int i = 0; i < 10; i++) {
        gpio_init(pontos[i]);
        gpio_set_dir(pontos[i], GPIO_OUT);
        gpio_put(pontos[i], 0);
    }

    for(int i = 0; i < 4; i++) {
        gpio_init(botoes_pin[i]);
        gpio_set_dir(botoes_pin[i], GPIO_IN);
        gpio_pull_up(botoes_pin[i]);

        gpio_set_irq_enabled_with_callback(
            botoes_pin[i], GPIO_IRQ_EDGE_FALL, true, &btn_callback
        );

        gpio_init(leds_pin[i]);
        gpio_set_dir(leds_pin[i], GPIO_OUT);
    }

    gpio_init(buzzer);
    gpio_set_dir(buzzer, GPIO_OUT);
}

// ================= JOGO =================
void limpar_pontuacao(void) {
    for(int i = 0; i < 10; i++) {
        gpio_put(pontos[i], 0);
    }
}

void atualizar_pontuacao(void) {
    if (jogo.tamanho > 0 && jogo.tamanho <= 10) {
        gpio_put(pontos[jogo.tamanho - 1], 1);
    }
}

// ISR SEM LOOPS - processa apenas um botão por vez
void btn_callback(uint gpio, uint32_t events) {
    if (events == 0x4) {
        if (!jogo.flg_rodando) {
            flg_inicio = 1;
        } else {
            if (jogo.aguardando_jogada && !jogo.btn_p) {
                uint32_t tempo_atual = to_ms_since_boot(get_absolute_time());
                
                // Verifica qual botão foi pressionado (sem loop)
                if (gpio == btn_r && (tempo_atual - ultimo_tempo_botao[0] > DEBOUNCE_TIME_MS)) {
                    ultimo_tempo_botao[0] = tempo_atual;
                    flg_botao_pressionado = 0;
                }
                else if (gpio == btn_g && (tempo_atual - ultimo_tempo_botao[1] > DEBOUNCE_TIME_MS)) {
                    ultimo_tempo_botao[1] = tempo_atual;
                    flg_botao_pressionado = 1;
                }
                else if (gpio == btn_b && (tempo_atual - ultimo_tempo_botao[2] > DEBOUNCE_TIME_MS)) {
                    ultimo_tempo_botao[2] = tempo_atual;
                    flg_botao_pressionado = 2;
                }
                else if (gpio == btn_y && (tempo_atual - ultimo_tempo_botao[3] > DEBOUNCE_TIME_MS)) {
                    ultimo_tempo_botao[3] = tempo_atual;
                    flg_botao_pressionado = 3;
                }
            }
        }
    }
}

void new_game() {
    srand(time(NULL));
    jogo.tamanho = 0;
    jogo.flg_rodando = 1;
    jogo.aguardando_jogada = 0;
    jogo.btn_p = 0;
    jogo.n_jogada = 0;
    jogo.btn_c = 0;

    for(int i = 0; i < 4; i++) {
        ultimo_tempo_botao[i] = 0;
    }
    flg_botao_pressionado = -1;

    limpar_pontuacao();

    multicore_fifo_push_blocking(1);
    next_level();
}

void acender_led(int cor, int tempo_ms) {
    gpio_put(leds_pin[cor], 1);
    sleep_ms(tempo_ms);
    gpio_put(leds_pin[cor], 0);
}

void tocar_som(int cor) {
    const int frequencias[4] = {262, 294, 330, 349};

    for(int i = 0; i < 50; i++) {
        gpio_put(buzzer, 1);
        sleep_us(500000 / frequencias[cor]);
        gpio_put(buzzer, 0);
        sleep_us(500000 / frequencias[cor]);
    }
}

void next_level() {
    if (jogo.tamanho <= 10) {
        jogo.sequencia[jogo.tamanho] = rand() % 4;
        jogo.tamanho++;

        for(int i = 0; i < jogo.tamanho; i++) {
            int cor = jogo.sequencia[i];
            acender_led(cor, 300);
            multicore_fifo_push_blocking(10 + cor);
            sleep_ms(200);
        }

        jogo.aguardando_jogada = 1;
        jogo.btn_p = 0;
        jogo.n_jogada = 0;
        jogo.btn_c = 0;

        iniciar_timer();

        flg_botao_pressionado = -1;
    } else {
        jogo.flg_rodando = 0;
        jogo.aguardando_jogada = 0;
        jogo.btn_p = 0;

        if (jogo.id_alarme != -1) {
            cancel_alarm(jogo.id_alarme);
            jogo.id_alarme = -1;
        }

        for(int j = 0; j < 3; j++) {
            for(int i = 0; i < 4; i++) gpio_put(leds_pin[i], 1);
            sleep_ms(200);
            for(int i = 0; i < 4; i++) gpio_put(leds_pin[i], 0);
            sleep_ms(200);
        }

        multicore_fifo_push_blocking(2);
    }
}

void perdeu() {
    jogo.flg_rodando = 0;
    jogo.aguardando_jogada = 0;
    jogo.btn_p = 0;
    jogo.n_jogada = 0;
    jogo.btn_c = 0;

    if (jogo.id_alarme != -1) {
        cancel_alarm(jogo.id_alarme);
        jogo.id_alarme = -1;
    }

    limpar_pontuacao();

    for(int j = 0; j < 3; j++) {
        for(int i = 0; i < 4; i++) gpio_put(leds_pin[i], 1);
        sleep_ms(200);
        for(int i = 0; i < 4; i++) gpio_put(leds_pin[i], 0);
        sleep_ms(200);
    }

    multicore_fifo_push_blocking(3);
}

void jogada() {
    if (!jogo.aguardando_jogada) return;

    sleep_ms(10);

    if (jogo.btn_c) return;

    int botao = flg_botao_pressionado;
    
    if (botao != -1) {
        flg_botao_pressionado = -1;
        jogo.btn_p = 1;
        jogo.btn_c = 1;

        acender_led(botao, 150);
        multicore_fifo_push_blocking(10 + botao);

        sleep_ms(50);

        if(botao == jogo.sequencia[jogo.n_jogada]) {
            jogo.n_jogada++;

            iniciar_timer();

            if(jogo.n_jogada >= jogo.tamanho) {
                atualizar_pontuacao();
                jogo.n_jogada = 0;
                jogo.aguardando_jogada = 0;
                jogo.btn_p = 0;
                jogo.btn_c = 0;
                sleep_ms(500);
                next_level();
            } else {
                jogo.btn_p = 0;
                jogo.btn_c = 0;
            }
        } else {
            perdeu();
        }
    } else {
        jogo.btn_p = 0;
    }
}

// ================= MAIN =================
int main() {
    stdio_init_all();
    botoes();

    multicore_launch_core1(core1_entry);

    while (true) {
        if (flg_inicio) {
            flg_inicio = 0;
            new_game();
        }

        if (jogo.flg_rodando) {
            jogada();
        }
        
        // Verifica se o timer expirou
        if (!jogo.flg_rodando && jogo.aguardando_jogada) {
            perdeu();
        }

        sleep_ms(10);
    }

    return 0;
}