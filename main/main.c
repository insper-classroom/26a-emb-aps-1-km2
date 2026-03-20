/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdlib.h> // 
#include <time.h> //

#define btn_r 15
#define btn_g 14
#define btn_b 12
#define btn_y 13

#define led_r 18
#define led_g 19
#define led_b 21
#define led_y 20

#define led_1 22
#define led_2 23
#define led_3 24
#define led_4 25
#define led_5 26
#define led_6 27
#define led_7 28
#define led_8 29
#define led_9 30
#define led_10 31


// #define audio 9
#define buzzer 28

#define DEBOUNCE_TIME_MS 100

const int pontos[10] = {led_1, led_2, led_3, led_4, led_5, led_6, led_7, led_8, led_9, led_10};
const int botoes_pin[4] = {btn_r, btn_g, btn_b, btn_y};
const int leds_pin[4] = {led_r, led_g, led_b, led_y};

int sequencia[20]; //
int tamanho = 0; //
volatile int flg_inicio = 0;
volatile int flg_rodando = 0; //
volatile int flg_botao[4] = {0, 0, 0, 0};
volatile int aguardando_jogada = 0;
volatile uint32_t ultimo_tempo_botao[4] = {0, 0, 0, 0};
volatile int btn_p = 0; //botao pensando

//
void btn_callback(uint gpio, uint32_t events);
void botoes(void);
void new_game(void);
void next_level(void);
void perdeu(void);
void jogada(void);
void pontuacao(int tamanho);
void acender_led(int cor, int tempo_ms);
void tocar_som(int cor);
//

void botoes() {
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
        // Som de vitoria 
    }
}

void perdeu() {
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
    // Som de erro 
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