#include "audio.h"
#include "hardware.h"
#include "pico/multicore.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"

#include "win.h"
#include "lose.h"
#include "music.h"

#define VOLUME_PCT 5

typedef struct {
    volatile const uint8_t* som_atual;
    volatile uint32_t tamanho_atual;
    volatile uint32_t wav_position;
    volatile bool tocando;
    volatile bool audio_loop;
} AudioState;

static AudioState g_audio = {0};

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

static void tocar_audio(const uint8_t* som, uint32_t tamanho, bool loop) {
    irq_set_enabled(PWM_IRQ_WRAP, false);
    g_audio.som_atual = som;
    g_audio.tamanho_atual = tamanho;
    g_audio.wav_position = 0;
    g_audio.audio_loop = loop;
    g_audio.tocando = true;
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

void audio_init(void) {
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

void audio_core1_entry(void) {
    audio_init();
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

void tocar_musica_fundo(void) {
    multicore_fifo_push_blocking(1);
}

void tocar_som_vitoria(void) {
    multicore_fifo_push_blocking(2);
}

void tocar_som_derrota(void) {
    multicore_fifo_push_blocking(3);
}

void tocar_som_botao(int cor) {
    multicore_fifo_push_blocking(10 + cor);
}