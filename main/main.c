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
#define TEMPO_LIMITE_MS 3000
#define MAX_SEQUENCIA 20
#define NUM_BOTOES 4
#define NUM_PONTOS 10

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
} JogoState;

// ================= ESTRUTURA DE ÁUDIO =================
typedef struct {
    volatile const uint8_t* som_atual;
    volatile uint32_t tamanho_atual;
    volatile uint32_t wav_position;
    volatile bool tocando;
    volatile bool audio_loop;
} AudioState;

// ================= VARIÁVEIS GLOBAIS =================
static JogoState g_jogo = {0};
static AudioState g_audio = {0};
volatile int flg_inicio = 0;
volatile int flg_botao[NUM_BOTOES] = {0};
volatile uint32_t ultimo_tempo_botao[NUM_BOTOES] = {0};

// ================= PROTÓTIPOS =================
void core1_entry();
void btn_callback(uint gpio, uint32_t events);
void botoes_init(void);
void new_game(void);
void next_level(void);
void perdeu(void);
void jogada(void);
void atualizar_pontuacao(void);
void limpar_pontuacao(void);
void acender_led(int cor, int tempo_ms);
void tocar_som_buzzer(int cor);
void iniciar_timer(void);
void cancelar_timer(void);

// ================= TIMER =================
int64_t timeout_callback(alarm_id_t id, void *user_data) {
    if (g_jogo.aguardando_jogada) {
        g_jogo.flg_timeout = 1;
    }
    return 0;
}

void iniciar_timer() {
    if (g_jogo.id_alarme != -1) {
        cancel_alarm(g_jogo.id_alarme);
    }
    g_jogo.id_alarme = add_alarm_in_ms(TEMPO_LIMITE_MS, timeout_callback, NULL, false);
}

void cancelar_timer() {
    if (g_jogo.id_alarme != -1) {
        cancel_alarm(g_jogo.id_alarme);
        g_jogo.id_alarme = -1;
    }
}

// ================= PWM IRQ =================
void pwm_interrupt_handler() {
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
            tocar_som_buzzer(cmd - 10);
        }
    }
}

// ================= HARDWARE =================
void botoes_init() {
    for(int i = 0; i < NUM_PONTOS; i++) {
        gpio_init(pontos[i]);
        gpio_set_dir(pontos[i], GPIO_OUT);
        gpio_put(pontos[i], 0);
    }

    for(int i = 0; i < NUM_BOTOES; i++) {
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
    for(int i = 0; i < NUM_PONTOS; i++) {
        gpio_put(pontos[i], 0);
    }
}

void atualizar_pontuacao(void) {
    if (g_jogo.tamanho > 0 && g_jogo.tamanho <= NUM_PONTOS) {
        gpio_put(pontos[g_jogo.tamanho - 1], 1);
    }
}

void btn_callback(uint gpio, uint32_t events) {
    if (events == GPIO_IRQ_EDGE_FALL) {
        if (!g_jogo.rodando) {
            flg_inicio = 1;
            return;
        }

        if (g_jogo.aguardando_jogada && !g_jogo.btn_p) {
            uint32_t tempo_atual = to_ms_since_boot(get_absolute_time());

            for(int i = 0; i < NUM_BOTOES; i++) {
                if (gpio == botoes_pin[i] && 
                    (tempo_atual - ultimo_tempo_botao[i] > DEBOUNCE_TIME_MS)) {
                    ultimo_tempo_botao[i] = tempo_atual;
                    flg_botao[i] = 1;
                    break;
                }
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

void new_game() {
    srand(time(NULL));
    g_jogo.tamanho = 0;
    g_jogo.rodando = 1;
    g_jogo.aguardando_jogada = 0;
    g_jogo.btn_p = 0;
    g_jogo.flg_timeout = 0;

    for(int i = 0; i < NUM_BOTOES; i++) {
        flg_botao[i] = 0;
        ultimo_tempo_botao[i] = 0;
    }

    limpar_pontuacao();
    multicore_fifo_push_blocking(1);
    next_level();
}

void next_level() {
    if (g_jogo.tamanho <= 10) {
        g_jogo.sequencia[g_jogo.tamanho] = rand() % 4;
        g_jogo.tamanho++;

        for(int i = 0; i < g_jogo.tamanho; i++) {
            int cor = g_jogo.sequencia[i];
            acender_led(cor, 300);
            multicore_fifo_push_blocking(10 + cor);
            sleep_ms(200);
        }

        g_jogo.aguardando_jogada = 1;
        g_jogo.btn_p = 0;

        iniciar_timer();

        for(int i = 0; i < 4; i++) {
            flg_botao[i] = 0;
        }
    } else {
        g_jogo.rodando = 0;
        g_jogo.aguardando_jogada = 0;
        g_jogo.btn_p = 0;

        cancelar_timer();

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
    g_jogo.rodando = 0;
    g_jogo.aguardando_jogada = 0;
    g_jogo.btn_p = 0;

    cancelar_timer();
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
    static int n_jogada = 0;
    static int btn_c = 0;

    if (!g_jogo.aguardando_jogada) return;

    sleep_ms(10);

    if (btn_c) return;

    for(int i = 0; i < 4; i++) {
        if (flg_botao[i]) {
            g_jogo.btn_p = 1;
            btn_c = 1;
            flg_botao[i] = 0;

            acender_led(i, 150);
            multicore_fifo_push_blocking(10 + i);

            sleep_ms(50);

            if(i == g_jogo.sequencia[n_jogada]) {
                n_jogada++;

                iniciar_timer();

                if(n_jogada >= g_jogo.tamanho) {
                    atualizar_pontuacao();
                    n_jogada = 0;
                    g_jogo.aguardando_jogada = 0;
                    g_jogo.btn_p = 0;
                    btn_c = 0;
                    sleep_ms(500);
                    next_level();
                } else {
                    g_jogo.btn_p = 0;
                    btn_c = 0;
                }
            } else {
                perdeu();
                n_jogada = 0;
                g_jogo.btn_p = 0;
                btn_c = 0;
            }
            break;
        }
    }

    if (!btn_c) g_jogo.btn_p = 0;
}

// ================= MAIN =================
int main() {
    stdio_init_all();
    botoes_init();

    multicore_launch_core1(core1_entry);

    while (true) {
        if (flg_inicio) {
            flg_inicio = 0;
            new_game();
        }

        if (g_jogo.flg_timeout) {
            g_jogo.flg_timeout = 0;
            perdeu();
        }

        if (g_jogo.rodando) {
            jogada();
        }

        sleep_ms(10);
    }

    return 0;
}