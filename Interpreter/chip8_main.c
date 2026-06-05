#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "audiovisual.h"
#include <time.h>
#include <pthread.h>

#include <SDL2/SDL.h>

pthread_mutex_t thread_lock;

// Int to track the running state of the main loop
int running = 1;

// Simple function to get the font sprite's location in memory
// for all hex digits. Returns 80 on failure, which will be a
// blank sprite.
int font_get(char number){

        switch(number) {
            case 0:     return 0;
            case 1:     return 5;
            case 2:     return 10;
            case 3:     return 15;
            case 4:     return 20;
            case 5:     return 25;
            case 6:     return 30;
            case 7:     return 35;
            case 8:     return 40;
            case 9:     return 45;
            case 0xA:   return 50;
            case 0xB:   return 55;
            case 0xC:   return 60;
            case 0xD:   return 65;
            case 0xE:   return 70;
            case 0xF:   return 75;
        }

        return 80;
}



// Struct to pass to the 60hz thread
typedef struct {
    unsigned int* delay;
    unsigned int* sound;
    char* keys;
} refresh_data;

// Loop for a second thread to execute at 60hz
// Includes all SDL functionality.
void* refresh_thread (void* thread_args){

    refresh_data* refresh = (refresh_data*) thread_args;

    // Attempt to initialize SDL, abort on failure.
    int init = init_sdl();

    if(init != 0){
        printf("ERROR: failed to initialize SDL, aborting.\n");
        quit_sdl();
        return NULL;
    }

    // Main loop to decrement delay and sound timers and scan input
    while(1){

        pthread_mutex_lock(&thread_lock);

        if(*refresh->delay > 0){
            (*refresh->delay)--;
        }

        if(*refresh->sound > 0){
            beep();
            (*refresh->sound)--;
        }

        pthread_mutex_unlock(&thread_lock);

        // Update keyboard and check for a quit, exit the loop in case
        // of a quit
        int quit = update_state(refresh->keys);

        if(quit) {
            break;
        }

        update_display(&thread_lock);
        SDL_Delay(16);
    }

    quit_sdl();
    running = 0;
    return NULL;

}



// Function to load a ROM into the given memory chunk.
// If the file doesn't load return 1.
// If the file is too big for CHIP-8's memory, return 2.
// If it succeeds return 0.
int load(unsigned char* memory, unsigned char* filename){

    // Open the given file, return 1 if it fails.
    int rom_fd = open(filename, O_RDONLY);

    if(rom_fd == -1){
        printf("ERROR: Failed opening ROM %s\n", filename);

        return 1;
    }

    // Read the file descripter into memory
    read(rom_fd, memory + 0x200, 0xE00);

    return 0;
}



int main(int args, char* argv[]){


    // If no arguments are given, print an error and return.
    if (args == 0) {
        printf("ERROR: No arguments given.\n");
        return -1;
    }

    // Initialize virtual CPU components and memory.
    // Includes 4096 bytes of memory, 16 registers, a memory pointer,
    // a program counter, a 32 byte stack, a delay and sound timer,
    // and an (array to handle key presses?).
    unsigned char* memory = malloc(4096);
    unsigned char registers[16];
    unsigned short pointer;
    unsigned short pc = 512;
    unsigned short stack[64];
    unsigned char sp = 0;
    unsigned int delay_timer = 0;
    unsigned int sound_timer = 0;
    unsigned char keys[16];

    // Declare font data, then copy the font data to the start of the memory.
    unsigned char fonts[80] = {0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
                    0x20, 0x60, 0x20, 0x20, 0x70, // 1
                    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
                    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
                    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
                    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
                    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
                    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
                    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
                    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
                    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
                    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
                    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
                    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
                    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
                    0xF0, 0x80, 0xF0, 0x80, 0x80}; // F

    for(int i = 0; i < 80; i++){
        memory[i] = fonts[i];
    }

    // Attempt to load the given ROM into memory, abort on failure.
    int load_result = load(memory, argv[1]);

    if(load_result != 0){
        printf("Failure loading ROM %s, aborting. Try a compatible ROM.\n", argv[1]);
        return -1;
    }

    // Create a secondary thread to run 60hz functionality
    pthread_t thread;
    refresh_data* thread_args = malloc(sizeof(refresh_data));
    thread_args->delay = &delay_timer;
    thread_args->sound = &sound_timer;
    thread_args->keys = keys;

    pthread_mutex_init(&thread_lock, NULL);

    pthread_create(&thread, NULL, &refresh_thread, (void *) thread_args);

    // Pass the current time as a seed for random functionality
    srand(time(NULL));

    // NOTE: In the opcode assembly notation, the first hex number represents the class of command.
    // If the command includes one or two registers, the sequentially first one is represented as
    // X and the second is represented as Y. The final 1, 2, and 3 hex numbers in the opcode are to be stored as
    // xxxN, xxNN, and xNNN, respectively, and one of these numbers will have bearing on all commands.
    while(1){

        if(!running){
            printf("Quit request received, aborting.\n");
            break;
        }

        // Extract opcode command
        unsigned char command = memory[pc] >> 4;
        command &= 0x0F;

        // Extract possible opcode registers
        unsigned short reg_X = memory[pc] & 0x0F;
        unsigned char reg_Y = memory[pc + 1] >> 4 & 0x0F;

        // Extract possible opcode specification numbers
        unsigned char op_xxxN = memory[pc + 1] & 0x0F;
        unsigned char op_xxNN = memory[pc + 1];
        unsigned short op_xNNN = (memory[pc + 1] & 0x00FF) | (reg_X << 8);

        // Decode opcode into command
        switch (command) {

            case 0x0:

                // CLEAR DISPLAY
                if(op_xxNN == 0xE0){
                    clear_display(&thread_lock);

                // RETURN FROM SUBROUTINE
                } else if(op_xxNN == 0xEE){

                    // Pop the return address from the stack
                    sp--;
                    pc = stack[sp];

                }

                break;

            // JUMP TO ADDRESS
            case 0x1:

                // Jump to the address before xNNN
                pc = op_xNNN - 2;
                break;

            // CALL SUBROUTINE
            case 0x2:

                // Set pc to the subroutine's previous address
                // and push the next address after returning to the stack
                stack[sp] = pc;
                sp++;
                pc = op_xNNN - 2;
                break;

            // SKIP OP IF reg_X == xxNN
            case 0x3:

                if(registers[reg_X] == op_xxNN){
                    pc += 2;
                }

                break;

            // SKIP OP IF reg_X != xxNN
            case 0x4:

                if(registers[reg_X] != op_xxNN){
                    pc += 2;
                }

                break;

            // SKIP OP IF reg_X == reg_Y
            case 0x5:

                if(registers[reg_X] == registers[reg_Y]){
                    pc += 2;
                }

                break;

            // SET reg_X TO xxNN
            case 0x6:

                registers[reg_X] = op_xxNN;
                break;

            // ADD xxNN TO reg_X
            case 0x7:

                registers[reg_X] += op_xxNN;
                break;


            case 0x8:

                switch(op_xxxN){

                    // SET reg_X TO reg_Y
                    case 0:

                        registers[reg_X] = registers[reg_Y];
                        break;

                    // SET reg_X |= reg_Y
                    case 1:

                        registers[reg_X] |= registers[reg_Y];
                        break;

                    // SET reg_X &= reg_Y
                    case 2:

                        registers[reg_X] &= registers[reg_Y];
                        break;

                    // SET reg_X ^= reg_Y
                    case 3:

                        registers[reg_X] ^= registers[reg_Y];
                        break;

                    // ADD reg_Y to reg_X, set carry flag in the case of an overflow
                    case 4:

                        int overflow = 0;

                        if (registers[reg_Y] + registers[reg_X] > 255){
                            overflow = 1;
                        } else {
                            overflow = 0;
                        }

                        registers[reg_X] += registers[reg_Y];
                        registers[0xF] = overflow;
                        break;

                    // SUBTRACT reg_Y from reg_X, set carry flag in the case of an underflow
                    case 5:

                        int underflow = 0;

                        if (registers[reg_X] < registers[reg_Y]){
                            underflow = 0;
                        } else {
                            underflow = 1;
                        }

                        registers[reg_X] -= registers[reg_Y];
                        registers[0xF] = underflow;
                        break;

                    // SHIFT reg_X right one bit, store the shifted bit in reg_F
                    case 6:

                        int right_set = 0b1 & registers[reg_X];
                        registers[reg_X] = registers[reg_X] >> 1;
                        registers[0xF] = right_set;
                        break;

                    // SUBTRACT reg_X from reg_Y, store in reg_X
                    case 7:

                        if (registers[reg_Y] < registers[reg_X]){
                            underflow = 0;
                        } else {
                            underflow = 1;
                        }

                        registers[reg_X] = registers[reg_Y] - registers[reg_X];
                        registers[0xF] = underflow;
                        break;

                    // SHIFT reg_X left one bit, store the shifted bit in reg_F
                    case 0xE:

                        int left_set = 0b10000000 & registers[reg_X];
                        registers[reg_X] = registers[reg_X] << 1;
                        registers[0xF] = left_set ? 1 : 0;
                        break;

                }

                break;

            // SKIP next op if reg_X != reg_Y
            case 0x9:

                if(registers[reg_X] != registers[reg_Y]) {
                    pc += 2;
                }
                break;

            // SET pointer TO xNNN
            case 0xA:

                pointer = op_xNNN;
                break;

            // GOTO reg_0 + op_xNNN
            case 0xB:

                // Set pc to the previous instruction from the desired one
                pc = registers[0] + op_xNNN - 2;
                break;

            // SET reg_X to a random char & xxNN
            case 0xC:

                registers[reg_X] = rand() & op_xxNN;
                break;

            // DRAW pointer SPRITE AT LOCATION reg_X, reg_Y WITH HEIGHT op_xxxN
            case 0xD:

                // Check that the sprite and height do not walk off the allotted memory,
                // if they do print an error message and abort. If not then draw the sprite.
                if (pointer + op_xxxN - 1 > 4096) {
                    printf("Error: sprite of height %d requires nonexistant memory", op_xxNN);
                    return 1;
                }

                int flipped = draw_sprite(memory + pointer, op_xxxN, registers[reg_X], registers[reg_Y],
                                          &thread_lock);

                // If the sprite flipped a preset pixel, set reg_F to 1
                if(flipped) {
                    registers[0xF] = 1;
                } else {
                    registers[0xF] = 0;
                }

                break;

            // SKIP the next operation based on if reg_X is pressed or not pressed, depending
            // on the value of xxNN. 9E corresponds to the former and A1 corresponds to the latter
            case 0xE:

                if(op_xxNN == 0x9E){

                    if(keys[registers[reg_X]]){
                        pc += 2;
                    }

                } else if(op_xxNN == 0xA1){

                    if(keys[registers[reg_X]] == 0){
                        pc += 2;
                    }

                }

                break;

            case 0xF:

                switch(op_xxNN){

                    // ASSIGN reg_X to the delay timer
                    case 0x07:

                        registers[reg_X] = delay_timer;
                        break;

                    // ASSIGN reg_X to the next key pressed
                    case 0x0A:

                        char keypressed = key_wait(keys);
                        registers[reg_X] = keypressed;
                        break;

                    // ASSIGN delay timer to reg_X
                    case 0x15:

                        pthread_mutex_lock(&thread_lock);
                        delay_timer = registers[reg_X];
                        pthread_mutex_unlock(&thread_lock);

                        break;

                    // ASSIGN sound timer to reg_X
                    case 0x18:

                        pthread_mutex_lock(&thread_lock);
                        sound_timer = registers[reg_X];
                        pthread_mutex_unlock(&thread_lock);

                        break;

                    // ASSIGN pointer += reg_X
                    case 0x1E:

                        pointer += registers[reg_X];
                        break;

                    // ASSIGN pointer to the font location of reg_X's char
                    case 0x29:

                        pointer = font_get(registers[reg_X]);
                        break;

                    // Store the decimal representation of reg_X in the pointer and following locations
                    case 0x33:

                        unsigned int hundreds = registers[reg_X] / 100;
                        unsigned int tens = (registers[reg_X] - hundreds * 100) / 10;
                        unsigned int ones = (registers[reg_X] - hundreds * 100 - tens * 10);
                        memory[pointer] = hundreds;
                        memory[pointer + 1] = tens;
                        memory[pointer + 2] = ones;
                        break;

                    // DUMP registers from reg_0 to reg_X starting at pointer
                    case 0x55:

                        for(int i = 0; i <= reg_X; i++){
                            memory[pointer + i] = registers[i];
                        }

                        pointer += reg_X + 1;

                        break;

                    // LOAD memory starting at pointer into registers reg_0 to reg_X
                    case 0x65:

                        for(int i = 0; i <= reg_X; i++){
                            registers[i] = memory[pointer + i];
                        }

                        pointer += reg_X + 1;

                        break;
                }

                break;
        }

        //Print newline, move to next instruction
        pc+= 2;
        SDL_Delay(1);

    }

    free(thread_args);
    free(memory);
    return 0;
}


