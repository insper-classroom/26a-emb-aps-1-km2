#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stdbool.h>

void audio_init(void);
void audio_core1_entry(void);
void tocar_musica_fundo(void);
void tocar_som_vitoria(void);
void tocar_som_derrota(void);
void tocar_som_botao(int cor);

#endif