/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <time.h> //pode?

#define btn_r 1
#define btn_g 2
#define btn_b 3
#define btn_y 4

#define led_r 5
#define led_g 6
#define led_b 7
#define led_y 8

#define audio 9
#define buzzer 10

const int botoes[4] = {btn_r, btn_g, btn_b, btn_y};
const int leds[4] = {led_r, led_g, led_b, led_y};

int sequencia[20]; 
int tamanho = 0;
int flg_inicio=0;
int flg_rodando=0;
volatile int flg_botao[4] = {0, 0, 0, 0};

void botoes() {
    for(int i = 0; i < 4; i++) {
        gpio_init(botoes[i]);
        gpio_set_dir(botoes[i], GPIO_IN);
        gpio_pull_up(botoes[i]); 

        gpio_set_irq_enabled_with_callback(botoes[i], GPIO_IRQ_EDGE_FALL, true, &btn_callback);

        gpio_init(leds[i]);
        gpio_set_dir(leds[i], GPIO_OUT);
    }

    gpio_init(buzzer);
    gpio_set_dir(buzzer, GPIO_OUT);

    //inicializar audio
}

void btn_callback(uint gpio, uint32_t events) {
  if (events == 0x4) { 
    if (!flg_rodando){
        flg_inicio=1;
    }else{
        for(int i = 0; i < 4; i++) {
            if(gpio == botoes[i]) {
                flg_botao[i] = 1;
            }
    }
    }
  }
    
}

void new_game() {
    srand(time(NULL));
    tamanho = 0;
    flg_rodando=1;
    
    next_level();
}

void acender_led(int cor, int tempo_ms) {
    gpio_put(leds[cor], 1);
    sleep_ms(tempo_ms);
    gpio_put(leds[cor], 0);
}

void tocar_som(int cor) {

    int frequencias[4] = {262, 294, 330, 349}; // Dó, Ré, Mi, Fá
    
    for(int i = 0; i < 100; i++) {
        gpio_put(buzzer, 1);
        sleep_us(500000 / frequencias[cor]);  // Meio período
        gpio_put(buzzer, 0);
        sleep_us(500000 / frequencias[cor]);
    }
}

void next_level(){
    if (tamanho< 20){

        sequencia[tamanho] = rand() % 4;
        tamanho++;
        for(int i = 0; i < tamanho; i++) {
            int cor = sequencia[i];
            acender_led(cor, 300);  
            tocar_som(cor);
            sleep_ms(200);  
        }
    }else{
        flg_rodando = 0;
        for(int j = 0; j < 3; j++) {
            for(int i = 0; i < 4; i++) {
                gpio_put(leds[i], 1);
            }
            sleep_ms(200);
            for(int i = 0; i < 4; i++) {
                gpio_put(leds[i], 0);
            }
            sleep_ms(200);
        }
        // Som de vitoria 
    
    
    }
    
}

void perdeu() {
   
    for(int j = 0; j < 3; j++) {
        for(int i = 0; i < 4; i++) {
            gpio_put(leds[i], 1);
        }
        sleep_ms(200);
        for(int i = 0; i < 4; i++) {
            gpio_put(leds[i], 0);
        }
        sleep_ms(200);
    }
    // Som de erro 
    
}



void jogada (){
    int n_jogada = 0;
    
    for(int i = 0; i < 4; i++) {
        if (flg_botao[i]){
            flg_botao[i]= 0;

            acender_led(i, 100);
            tocar_som(i);
            
 
            if(i == sequencia[n_jogada]) {
    
                n_jogada++;

                if(n_jogada >= tamanho) {
                    n_jogada = 0;
                    sleep_ms(1000);
                    next_level();  
                }
            }else{
                erro();

                flg_rodando=0;
                n_jogada=0;
            }
        }
    }
}




int main() {
    stdio_init_all();
    botoes();

    while (true) {
       if(flg_inicio) {
            flg_inicio = 0; 
            new_game();
        }
        if (flg_rodando){
            jogada();
        }
        sleep_ms(10);
    }
    return 0;
}
