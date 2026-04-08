#ifndef HARDWARE_H
#define HARDWARE_H

#include "pico/stdlib.h"

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

#define NUM_BOTOES 4
#define NUM_PONTOS 10


extern const int pontos[NUM_PONTOS];
extern const int botoes_pin[NUM_BOTOES];
extern const int leds_pin[NUM_BOTOES];


void hardware_init(void);
void acender_led(int cor, int tempo_ms);
void tocar_som_buzzer(int cor);
void limpar_pontuacao(void);
void atualizar_pontuacao(int tamanho);

#endif