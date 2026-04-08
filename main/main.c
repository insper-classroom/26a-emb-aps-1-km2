#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware.h"
#include "audio.h"
#include "jogo.h"

int main(void) {
    JogoState jogo;
    
    jogo_init(&jogo);
    ptr_jogo_irq = &jogo;
    
    stdio_init_all();
    hardware_init();
    botoes_init_com_irq();
    
    multicore_launch_core1(audio_core1_entry);

    while (true) {
        jogo_processar(&jogo);
        sleep_ms(10);
    }

    return 0;
}