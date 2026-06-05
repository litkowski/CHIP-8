#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <pthread.h>

int init_sdl();
int clear_display(pthread_mutex_t* lock);
void update_display(pthread_mutex_t* lock);
int key_wait(char keys[16]);
int update_state(char* keys);
int draw_sprite(char* sprite, char height, char x, char y, pthread_mutex_t* lock);
int quit_sdl();
void beep();
