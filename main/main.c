#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdlib.h> // 
#include <time.h> //
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"

#include "win.h"
#include "lose.h"
#include "music.h"

#define btn_r 15
#define btn_g 14
#define btn_b 12
#define btn_y 13

#define led_r 18
#define led_g 19
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


// #define audio 9
#define buzzer 28

#define AUDIO_PIN 16

#define DEBOUNCE_TIME_MS 100

#define VOLUME_PCT 5  

const int pontos[10] = { led_10, led_9, led_8, led_7, led_6, led_5, led_4, led_3, led_2, led_1};
const int botoes_pin[4] = {btn_r, btn_g, btn_b, btn_y};
const int leds_pin[4] = {led_r, led_g, led_b, led_y};

int sequencia[20]; //
int tamanho = 0; //
volatile int flg_inicio = 0;
volatile int flg_rodando = 0; //
volatile int flg_botao[4] = {0, 0, 0, 0};
volatile int aguardando_jogada = 0;
volatile uint32_t ultimo_tempo_botao[4] = {0, 0, 0, 0}; //
volatile int btn_p = 0; //botao pensando

const uint8_t* som_atual = NULL;
uint32_t tamanho_atual = 0;
uint32_t wav_position = 0;
volatile bool tocando = false;
volatile bool audio_loop = false;

//
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
//


void pwm_interrupt_handler() {
    pwm_clear_irq(pwm_gpio_to_slice_num(AUDIO_PIN));

    if (som_atual != NULL) {
        if (wav_position < (tamanho_atual << 3)) {
            uint8_t sample = som_atual[wav_position >> 3];

            // reduz volume sem distorcer tanto
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

void tocar_musica_fundo(void) {
    tocar_audio(WAV_DATA_MUSIC, WAV_DATA_LENGTH_MUSIC, true);
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

    pwm_set_gpio_level(AUDIO_PIN, 0);
}

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

        gpio_set_irq_enabled_with_callback(botoes_pin[i], GPIO_IRQ_EDGE_FALL, true, &btn_callback);

        gpio_init(leds_pin[i]);
        gpio_set_dir(leds_pin[i], GPIO_OUT);
    }

    gpio_init(buzzer);
    gpio_set_dir(buzzer, GPIO_OUT);
}

void limpar_pontuacao(void) {
    for(int i = 0; i < 10; i++) {
        gpio_put(pontos[i], 0);
    }
}

void atualizar_pontuacao(void) {
    if (tamanho > 0 && tamanho <= 10) {
        gpio_put(pontos[tamanho - 1], 1);
    }
}


void btn_callback(uint gpio, uint32_t events) {
    if (events == 0x4) { 
        if (!flg_rodando) {
            flg_inicio = 1;
        } else {
            if (aguardando_jogada && !btn_p) { 
                uint32_t tempo_atual = to_ms_since_boot(get_absolute_time());
                
           
                if (gpio == btn_r && flg_botao[0] == 0 && 
                    (tempo_atual - ultimo_tempo_botao[0] > DEBOUNCE_TIME_MS)) {
                    ultimo_tempo_botao[0] = tempo_atual;
                    flg_botao[0] = 1;

                } else if (gpio == btn_g && flg_botao[1] == 0 && 
                           (tempo_atual - ultimo_tempo_botao[1] > DEBOUNCE_TIME_MS)) {
                    ultimo_tempo_botao[1] = tempo_atual;
                    flg_botao[1] = 1;

                } else if (gpio == btn_b && flg_botao[2] == 0 && 
                           (tempo_atual - ultimo_tempo_botao[2] > DEBOUNCE_TIME_MS)) {
                    ultimo_tempo_botao[2] = tempo_atual;
                    flg_botao[2] = 1;

                } else if (gpio == btn_y && flg_botao[3] == 0 && 
                           (tempo_atual - ultimo_tempo_botao[3] > DEBOUNCE_TIME_MS)) {
                    ultimo_tempo_botao[3] = tempo_atual;
                    flg_botao[3] = 1;
                }
            }
        }
    }
}

void new_game() {
    srand(time(NULL));
    tamanho = 0;
    flg_rodando = 1;
    aguardando_jogada = 0;
    btn_p = 0;

    for(int i = 0; i < 4; i++) {
        flg_botao[i] = 0;
        ultimo_tempo_botao[i] = 0;
    }
    limpar_pontuacao();
    tocar_musica_fundo(); 
    next_level();
}

void acender_led(int cor, int tempo_ms) {
    gpio_put(leds_pin[cor], 1);
    sleep_ms(tempo_ms);
    gpio_put(leds_pin[cor], 0);
}

void tocar_som(int cor) {
    const int frequencias[4] = {262, 294, 330, 349}; // Dó, Ré, Mi, Fá
    
    for(int i = 0; i < 50; i++) {
        gpio_put(buzzer, 1);
        sleep_us(500000 / frequencias[cor]);  
        gpio_put(buzzer, 0);
        sleep_us(500000 / frequencias[cor]);
    }
}


void next_level() {
    if (tamanho <= 10) {
        sequencia[tamanho] = rand() % 4;
        tamanho++;

        for(int i = 0; i < tamanho; i++) {
            int cor = sequencia[i];
            acender_led(cor, 300);  
            tocar_som(cor);
            sleep_ms(200);  
        }

        //
        aguardando_jogada = 1;
        btn_p = 0; 
        for(int i = 0; i < 4; i++) {
            flg_botao[i] = 0;
        }
    } else {
        flg_rodando = 0;
        aguardando_jogada = 0;
        btn_p = 0;
        for(int j = 0; j < 3; j++) {
            for(int i = 0; i < 4; i++) {
                gpio_put(leds_pin[i], 1);
            }
            sleep_ms(200);
            for(int i = 0; i < 4; i++) {
                gpio_put(leds_pin[i], 0);
            }
            sleep_ms(200);
        }
        tocar_audio(WAV_DATA_WIN, WAV_DATA_LENGTH_WIN, false);
        
    }
}

void perdeu() {
    flg_rodando = 0;
    aguardando_jogada = 0;
    btn_p = 0;

    limpar_pontuacao();
   
    for(int j = 0; j < 3; j++) {
        for(int i = 0; i < 4; i++) {
            gpio_put(leds_pin[i], 1);
        }
        sleep_ms(200);
        for(int i = 0; i < 4; i++) {
            gpio_put(leds_pin[i], 0);
        }
        sleep_ms(200);
    }
    tocar_audio(WAV_DATA_LOSE, WAV_DATA_LENGTH_LOSE, false);
}

void jogada() {
    static int n_jogada = 0;
    static int btn_c = 0;// botao computado
    
   //
    if (!aguardando_jogada) {
        return;
    }
    
   
    sleep_ms(10);
    
    
    if (btn_c) {
        return;
    }
    
    for(int i = 0; i < 4; i++) {
        if (flg_botao[i]) {
            btn_p = 1;
            btn_c = 1;
            flg_botao[i] = 0;

            
            acender_led(i, 150);
            tocar_som(i);
            sleep_ms(50);
            
            if(i == sequencia[n_jogada]) {
                n_jogada++;

                if(n_jogada >= tamanho) {
                    atualizar_pontuacao();
                    n_jogada = 0;
                    aguardando_jogada = 0;
                    btn_p = 0;
                    btn_c = 0;
                    sleep_ms(500);
                    next_level();  
                } else {
                    btn_p = 0;
                    btn_c = 0;
                }
            } else {
                perdeu();
                n_jogada = 0;
                btn_p = 0; 
                btn_c = 0; 
            }
            break;
        }
    }
    
    if (!btn_c) {
        btn_p = 0;
    }
}

int main() {
    stdio_init_all();
    botoes();
    init_audio();

    while (true) {
        if (flg_inicio) {
            flg_inicio = 0; 
            new_game();
        }
        if (flg_rodando) {
            jogada();
        }
        sleep_ms(10);
    }
    return 0;
}