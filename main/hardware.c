#include "hardware.h"
#include "hardware/gpio.h"

const int pontos[NUM_PONTOS] = { led_10, led_9, led_8, led_7, led_6, led_5, led_4, led_3, led_2, led_1};
const int botoes_pin[NUM_BOTOES] = {btn_r, btn_g, btn_b, btn_y};
const int leds_pin[NUM_BOTOES] = {led_r, led_g, led_b, led_y};

void hardware_init(void) {
    for(int i = 0; i < NUM_PONTOS; i++) {
        gpio_init(pontos[i]);
        gpio_set_dir(pontos[i], GPIO_OUT);
        gpio_put(pontos[i], 0);
    }
    
    for(int i = 0; i < NUM_BOTOES; i++) {
        gpio_init(leds_pin[i]);
        gpio_set_dir(leds_pin[i], GPIO_OUT);
    }
    
    gpio_init(buzzer);
    gpio_set_dir(buzzer, GPIO_OUT);
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

void limpar_pontuacao(void) {
    for(int i = 0; i < NUM_PONTOS; i++) {
        gpio_put(pontos[i], 0);
    }
}

void atualizar_pontuacao(int tamanho) {
    if (tamanho > 0 && tamanho <= NUM_PONTOS) {
        gpio_put(pontos[tamanho - 1], 1);
    }
}