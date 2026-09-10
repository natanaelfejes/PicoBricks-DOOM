//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2021-2022 Graham Sanderson
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//     SDL implementation of system-specific input interface.
//


//#include "SDL.h"
//#include "SDL_keycode.h"
#include <doom/sounds.h>
#include <doom/s_sound.h>
#include "pico.h"
#include "doomkeys.h"
#include "doomtype.h"
#include "d_event.h"
#include "i_input.h"
#include "i_system.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_controls.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include <stdlib.h>
#if USB_SUPPORT
#include "pico/binary_info.h"
#include "tusb.h"
#include "hardware/irq.h"
bi_decl(bi_program_feature("USB keyboard support"));
#endif

#if JTHUMBY
#define GPIO_BUTTONS 1
#endif

#if JEMBRICK
#define ACCELEROMETER_SUPPORT 1
#define PIO_CAPSENSE 1
#endif

#if JEMRING
#define PIO_CAPSENSE 1
#endif

#if JPICOBRICKS
#define GPIO_BUTTON_ADC 1
#define GPIO_BUTTONS 1
#endif

#if ACCELEROMETER_SUPPORT
#define I2C_SDA_PIN 26
#define I2C_SCL_PIN 27
#include "pico/binary_info.h"
#include "hardware/i2c.h"
bi_decl(bi_2pins_with_func(I2C_SDA_PIN, I2C_SCL_PIN, GPIO_FUNC_I2C));
#endif

#if PIO_CAPSENSE
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "capsense.pio.h"

typedef struct {
    uint8_t pin;
    uint8_t sm;
    uint8_t scancode;
    uint8_t state;
    uint32_t zero;
} capsensor_t;

static capsensor_t capsensors[] = {
#if JEMBRICK
    { 0, 0, 44, 0, 0x0fffffff},
    { 1, 1, 224, 0, 0x0fffffff},
#elif JEMRING
    { 0, 0, 0x50, 0, 0}, // left
    { 1, 1, 0x52, 0, 0}, // up
    { 2, 2, 0x4f, 0, 0}, // right
    
    { 3, 3, 224, 0, 0}, // fire
#endif
};

static uint32_t capsense_window = 2048;
#endif

#if ACCELEROMETER_SUPPORT

#define ACC_ADDR 15
static bool acc_dir[6] = {0};

void acc_press(int tilt, int scancode, int mod, int axis) {
    if (tilt > 5) {
        if (!acc_dir[axis]) {
            acc_dir[axis] = true;

            event_t event;
            event.type = ev_keydown;
            event.data1 = TranslateKey(scancode);
            event.data2 = GetLocalizedKey(scancode);
            event.data3 = GetTypedChar(scancode, mod & WITH_SHIFT ? 1 : 0);
            D_PostEvent(&event);
        }
    } else if (tilt < 5)  {
        if (acc_dir[axis]) {
            acc_dir[axis] = false;

            event_t event;
            event.type = ev_keyup;
            event.data1 = TranslateKey(scancode);
            event.data2 = 0;
            event.data3 = 0;
            D_PostEvent(&event);
        }
    }
}

void accelerometer_init() {
    i2c_init(i2c1, 400000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    //gpio_pull_up(I2C_SDA_PIN);
    //gpio_pull_up(I2C_SCL_PIN);

    const uint8_t CTRL_REG1 = 0x1B;
    const uint8_t CTRL_REG2 = 0x1D;
    const uint8_t DATA_CTRL_REG = 0x21;
    //resolution = 16384

    uint8_t data[2];
    
    data[0] = CTRL_REG1;
    data[1] = 0;
    i2c_write_blocking(i2c1, ACC_ADDR, data, 2, false);

    data[0] = CTRL_REG2;
    data[1] = 0;
    i2c_write_blocking(i2c1, ACC_ADDR, data, 2, false);

    data[1] = 0x80;
    i2c_write_blocking(i2c1, ACC_ADDR, data, 2, false);

    sleep_ms(2);

    data[0] = CTRL_REG1;
    data[1] = 0b01000000;   // Stand-by, High-Res, Interrupts-Off, 2g, Wakeup-Disabled
    i2c_write_blocking(i2c1, ACC_ADDR, data, 2, false);

    data[0] = DATA_CTRL_REG;
    data[1] = 0b00000011;  // 100Hz
    i2c_write_blocking(i2c1, ACC_ADDR, data, 2, false);

    data[0] = CTRL_REG1;
    data[1] = 0b11000000;   // Operating, High-Res, Interrupts-Off, 2g, Wakeup-Disabled
    i2c_write_blocking(i2c1, ACC_ADDR, data, 2, false);
}

void accelerometer_getevent() {
    const uint8_t cmd_vec[] = {0x06};
    int8_t acc_vec[6] = {};
    i2c_write_blocking(i2c1, ACC_ADDR, cmd_vec, sizeof(cmd_vec), false);
    i2c_read_blocking(i2c1, ACC_ADDR, (uint8_t*)acc_vec, sizeof(acc_vec), false);
    acc_press( acc_vec[1], 0x52, 0, 0); //Up
    acc_press(-acc_vec[1], 0x51, 0, 1); //Down

    acc_press( acc_vec[5], 0x50, 0, 2); //Left
    acc_press(-acc_vec[5], 0x4f, 0, 3); //Right
}
#endif

#if PIO_CAPSENSE
void capsense_init() {
    PIO pio = pio0;
    uint offset = capsense_program_offset(pio);

    for (int i = 0; i < sizeof(capsensors) / sizeof(*capsensors); ++i) {
        capsensors[i].sm = pio_claim_unused_sm(pio, true);
        capsense_program_init(pio, capsensors[i].sm, offset, capsensors[i].pin);
    }
}

void capsense_getevent() {
    for (int i = 0; i < sizeof(capsensors) / sizeof(*capsensors); ++i) {
        uint32_t count = capsense_program_read(pio0, capsensors[i].sm);

        if (count < capsensors[i].zero) {
            capsensors[i].zero = count;
        } else if (count > capsensors[i].zero + capsense_window) {
            capsensors[i].zero = count - capsense_window;
        }

        count -= capsensors[i].zero;

        if (!capsensors[i].state && count > ((capsense_window * 3) / 4)) {
            capsensors[i].state = true;

            event_t event;
            event.type = ev_keydown;
            event.data1 = TranslateKey(capsensors[i].scancode);
            event.data2 = GetLocalizedKey(capsensors[i].scancode);
            event.data3 = GetTypedChar(capsensors[i].scancode, 0);
            D_PostEvent(&event);

        } else if (capsensors[i].state && count < (capsense_window / 4)) {
            capsensors[i].state = false;

            event_t event;
            event.type = ev_keyup;
            event.data1 = TranslateKey(capsensors[i].scancode);
            event.data2 = 0;
            event.data3 = 0;
            D_PostEvent(&event);
        }
    }
}
#endif

#if GPIO_BUTTONS

enum {
    BTN_L,
    BTN_U,
    BTN_R,
    BTN_D,
    BTN_1,
    BTN_2,
    BTN_COUNT
};

static const uint8_t button_pins[BTN_COUNT] = {
    3, 4, 5, 6, 24, 27
};
static uint8_t button_state[BTN_COUNT] = {0};

void buttons_init() {
    for (int i = 0; i < count_of(button_pins); ++i) {
        gpio_init(button_pins[i]);
        gpio_set_dir(button_pins[i], GPIO_IN);
        gpio_pull_up(button_pins[i]);
    }

    key_right = KEY_RIGHTARROW;
    key_left = KEY_LEFTARROW;
    key_up = KEY_UPARROW;
    key_down = KEY_DOWNARROW;

    key_fire = KEY_RCTRL;

    key_use = KEY_RSHIFT;
    key_strafe = KEY_RSHIFT;
    key_speed = KEY_RSHIFT;

    key_prevweapon = '[';
    key_nextweapon = ']';

    //key_menu_activate  = KEY_ESCAPE;
    key_menu_up        = KEY_UPARROW;
    key_menu_down      = KEY_DOWNARROW;
    key_menu_left      = KEY_LEFTARROW;
    key_menu_right     = KEY_RIGHTARROW;
    key_menu_back      = KEY_RSHIFT;
    key_menu_forward   = KEY_RCTRL;
    key_menu_confirm   = KEY_RCTRL;
    key_menu_abort     = KEY_RSHIFT;
}

void button_event(key_type_t key, bool pressed) {
    event_t event;
    if (pressed) {
        event.type = ev_keydown;
        event.data1 = key;
        event.data2 = key;
        event.data3 = key;
    } else {
        event.type = ev_keyup;
        event.data1 = key;
        event.data2 = 0;
        event.data3 = 0;
    }
    D_PostEvent(&event);
}


#if GPIO_BUTTON_ADC
extern uint8_t frame_buffer[2][128*64];
extern int display_frame_index;
extern uint8_t palette[256];

static const uint8_t tiny_font[37][3] = {
    {0x1F,0x11,0x1F}, {0x00,0x00,0x1F}, {0x1D,0x15,0x17}, {0x15,0x15,0x1F}, {0x07,0x04,0x1F}, {0x17,0x15,0x1D}, {0x1F,0x15,0x1D}, {0x01,0x01,0x1F}, {0x1F,0x15,0x1F}, {0x17,0x15,0x1F},
    {0x00,0x00,0x00}, {0x1F,0x05,0x1F}, {0x1F,0x15,0x0A}, {0x0E,0x11,0x11}, {0x1F,0x11,0x0E}, {0x1F,0x15,0x15}, {0x1F,0x05,0x05}, {0x0E,0x15,0x1D}, {0x1F,0x04,0x1F}, {0x11,0x1F,0x11},
    {0x08,0x10,0x0F}, {0x1F,0x04,0x1B}, {0x1F,0x10,0x10}, {0x1F,0x02,0x1F}, {0x1F,0x02,0x1C}, {0x0E,0x11,0x0E}, {0x1F,0x09,0x06}, {0x0E,0x15,0x1E}, {0x1F,0x09,0x16}, {0x12,0x15,0x09},
    {0x01,0x1F,0x01}, {0x0F,0x10,0x0F}, {0x07,0x18,0x07}, {0x0F,0x10,0x0F}, {0x1B,0x04,0x1B}, {0x03,0x1C,0x03}, {0x04,0x0A,0x04} // +
};

static void pb_draw_char_scaled(uint8_t *fb, int x, int y, char c, int scale) {
    int idx = 10;
    if (c >= '0' && c <= '9') idx = c - '0';
    else if (c >= 'A' && c <= 'Z') idx = c - 'A' + 11;
    else if (c >= 'a' && c <= 'z') idx = c - 'a' + 11;
    else if (c == '+') idx = 36;
    if (idx == 10) return;

    for (int col = 0; col < 3; col++) {
        uint8_t bits = tiny_font[idx][col];
        for (int row = 0; row < 5; row++) {
            if (bits & (1 << (4 - row))) {
                // Draw a scale x scale block
                for(int dy=0; dy<scale; dy++) {
                    for(int dx=0; dx<scale; dx++) {
                        int px = x + col*scale + dx;
                        int py = y + row*scale + dy;
                        if (px < 128 && py < 64) {
                            fb[py * 128 + px] = 255;
                        }
                    }
                }
            }
        }
    }
}

static void pb_draw_string_scaled(uint8_t *fb, int x, int y, const char *str, int scale) {
    while (*str) { 
        pb_draw_char_scaled(fb, x, y, *str, scale); 
        x += (3 * scale) + scale; // width + spacing
        str++; 
    }
}

void picobricks_splash(void) {
    uint8_t *fb = frame_buffer[display_frame_index];
    memset(fb, 0, 128 * 64);
    for (int i = 0; i < 256; i++) {
        palette[i] = (i > 128) ? 255 : 0;
    }

    pb_draw_string_scaled(fb, 4, 2, "PICOBRICKS DOOM", 2);
    
    pb_draw_string_scaled(fb, 8, 20, "DIAL TURN", 2);
    pb_draw_string_scaled(fb, 8, 32, "TAP  FIRE+USE", 2);
    pb_draw_string_scaled(fb, 8, 44, "HOLD BRAKE", 2);
    
    while (gpio_get(10) == 0) sleep_ms(10);
    while (gpio_get(10) == 1) sleep_ms(10);
    memset(fb, 0, 128 * 64);
}
#endif

#if GPIO_BUTTON_ADC
// PicoBricks: GP10 = fire button (active high), GP26 = potentiometer (ADC0) for turning
#include "hardware/adc.h"

static bool pb_btn_raw = false;
static uint32_t pb_press_start = 0;
static int click_count = 0;
static uint32_t click_timer = 0;
static bool is_braking = false;

static bool pb_fire_state = false;
static bool pb_use_state = false;
static bool pb_nextweap_state = false;
static bool pb_left_state = false;
static bool pb_right_state = false;
static bool pb_forward_state = false;

static uint32_t pb_release_fire_time = 0;
static uint32_t pb_release_use_time = 0;
static uint32_t pb_release_weap_time = 0;

static void picobricks_event(key_type_t key, bool pressed, bool *state) {
    if (pressed == *state) return;
    *state = pressed;
    event_t event;
    event.type  = pressed ? ev_keydown : ev_keyup;
    event.data1 = key;
    event.data2 = pressed ? key : 0;
    event.data3 = pressed ? key : 0;
    D_PostEvent(&event);
}

void picobricks_init() {
    gpio_init(J_BUTTON_PIN);
    gpio_set_dir(J_BUTTON_PIN, GPIO_IN);
    // Active high on PicoBricks (pulled down externally)
    gpio_pull_down(J_BUTTON_PIN);

    // Red LED
    gpio_init(7);
    gpio_set_dir(7, GPIO_OUT);
    gpio_put(7, 0);

    // Relay
    gpio_init(9);
    gpio_set_dir(9, GPIO_OUT);
    gpio_put(9, 0);

    adc_init();
    adc_gpio_init(J_POT_PIN);
    adc_select_input(0); 

    key_fire = KEY_RCTRL;
    key_left = KEY_LEFTARROW;
    key_right = KEY_RIGHTARROW;
    key_up = KEY_UPARROW;
    key_down = KEY_DOWNARROW;
    key_use = ' ';
    key_nextweapon = ']';
}

#include "doom/doomstat.h"
#include "doom/d_player.h"
static int pb_last_health = 100;
static uint32_t pb_relay_off_time = 0;

void picobricks_getevent() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool pressed = (gpio_get(J_BUTTON_PIN) == 1);
    
    if (pressed && !pb_btn_raw) {
        pb_btn_raw = true; pb_press_start = now;
    } else if (!pressed && pb_btn_raw) {
        pb_btn_raw = false;
        if (is_braking) {
            is_braking = false;
        } else {
            click_count++;
            click_timer = now;
        }
    } else if (pressed && pb_btn_raw) {
        if (now - pb_press_start > 200 && !is_braking) {
            is_braking = true;
            click_count = 0;
        }
    }

    if (click_count > 0 && now - click_timer > 250) {
        if (click_count == 1) {
            picobricks_event(key_fire, true, &pb_fire_state);
            pb_release_fire_time = now + 100;
        } else if (click_count == 2) {
            picobricks_event(key_use, true, &pb_use_state);
            pb_release_use_time = now + 100;
        } else if (click_count >= 3) {
            picobricks_event(key_nextweapon, true, &pb_nextweap_state);
            pb_release_weap_time = now + 100;
        }
        click_count = 0;
    }

    if (pb_fire_state && now > pb_release_fire_time) picobricks_event(key_fire, false, &pb_fire_state);
    if (pb_use_state && now > pb_release_use_time) picobricks_event(key_use, false, &pb_use_state);
    if (pb_nextweap_state && now > pb_release_weap_time) picobricks_event(key_nextweapon, false, &pb_nextweap_state);

    adc_select_input(0);
    uint16_t pot = adc_read();
    bool left_pressed  = (pot < J_POT_LEFT_THRESH);
    bool right_pressed = (pot > J_POT_RIGHT_THRESH);
    picobricks_event(key_left, left_pressed, &pb_left_state);
    picobricks_event(key_right, right_pressed, &pb_right_state);
        // Pulse the forward key to walk slower (smooth momentum in DOOM engine)
    bool slow_walk = !is_braking && ((now % 66) < 33); // 50% duty cycle
    picobricks_event(key_up, slow_walk, &pb_forward_state);

    gpio_put(7, pb_fire_state ? 1 : 0);

    if (playeringame[consoleplayer]) {
        int h = players[consoleplayer].health;
        if (h < pb_last_health) {
            gpio_put(9, 1);
            pb_relay_off_time = now + 100;
        }
        pb_last_health = h;
    }
    if (now > pb_relay_off_time) gpio_put(9, 0);
}
#endif

void buttons_getevent() {

    for (int i = 0; i < BTN_COUNT; ++i) {
        bool pressed = (gpio_get(button_pins[i]) == 0);
        if (pressed != (button_state[i] != 0)) {
            bool alt = (button_state[i] > 1);
            button_state[i] = pressed;

            switch (i) {
                case BTN_L:
                    button_event(key_left, pressed);
                    break;
                case BTN_U:
                    button_event(key_up, pressed);
                    break;
                case BTN_R:
                    button_event(key_right, pressed);
                    break;
                case BTN_D:
                    if (pressed && button_state[BTN_1]) {
                        button_state[i] = 3;
                        alt = true;
                    }
                    if (alt) {
                        button_event(key_nextweapon, pressed);
                    } else {
                        button_event(key_down, pressed);
                    }
                    break;
                case BTN_1:
                    button_event(key_use, pressed);
                    break;
                case BTN_2:
                    button_event(key_fire, pressed);
                    break;
            }
        }
    }
}

#endif


static const int scancode_translate_table[] = SCANCODE_TO_KEYS_ARRAY;

// Lookup table for mapping ASCII characters to their equivalent when
// shift is pressed on a US layout keyboard. This is the original table
// as found in the Doom sources, comments and all.
static const char shiftxform[] =
        {
                0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                31, ' ', '!', '"', '#', '$', '%', '&',
                '"', // shift-'
                '(', ')', '*', '+',
                '<', // shift-,
                '_', // shift--
                '>', // shift-.
                '?', // shift-/
                ')', // shift-0
                '!', // shift-1
                '@', // shift-2
                '#', // shift-3
                '$', // shift-4
                '%', // shift-5
                '^', // shift-6
                '&', // shift-7
                '*', // shift-8
                '(', // shift-9
                ':',
                ':', // shift-;
                '<',
                '+', // shift-=
                '>', '?', '@',
                'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
                'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
                '[', // shift-[
                '!', // shift-backslash - OH MY GOD DOES WATCOM SUCK
                ']', // shift-]
                '"', '_',
                '\'', // shift-`
                'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
                'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
                '{', '|', '}', '~', 127
        };

// If true, I_StartTextInput() has been called, and we are populating
// the data3 field of ev_keydown events.
static boolean text_input_enabled = true;

// Bit mask of mouse button state.
static unsigned int mouse_button_state = 0;

// Disallow mouse and joystick movement to cause forward/backward
// motion.  Specified with the '-novert' command line parameter.
// This is an int to allow saving to config file
int novert = 0;

// If true, keyboard mapping is ignored, like in Vanilla Doom.
// The sensible thing to do is to disable this if you have a non-US
// keyboard.

#if !USE_VANILLA_KEYBOARD_MAPPING_ONLY
int vanilla_keyboard_mapping = true;
#endif

// Mouse acceleration
//
// This emulates some of the behavior of DOS mouse drivers by increasing
// the speed when the mouse is moved fast.
//
// The mouse input values are input directly to the game, but when
// the values exceed the value of mouse_threshold, they are multiplied
// by mouse_acceleration to increase the speed.
#if !NO_USE_MOUSE
float mouse_acceleration = 2.0;
int mouse_threshold = 10;
#endif

enum {
    SDL_SCANCODE_SPACE = 44,
    SDL_SCANCODE_LCTRL = 224,
    SDL_SCANCODE_LSHIFT = 225,
    SDL_SCANCODE_LALT = 226, /**< alt, option */
    SDL_SCANCODE_LGUI = 227, /**< windows, command (apple), meta */
    SDL_SCANCODE_RCTRL = 228,
    SDL_SCANCODE_RSHIFT = 229,
    SDL_SCANCODE_RALT = 230, /**< alt gr, option */
    SDL_SCANCODE_RGUI = 231, /**< windows, command (apple), meta */
};

// Translates the SDL key to a value of the type found in doomkeys.h
int TranslateKey(int scancode)
{
    switch (scancode)
    {
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL:
            return KEY_RCTRL;

        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT:
            return KEY_RSHIFT;

        case SDL_SCANCODE_LALT:
            return KEY_LALT;

        case SDL_SCANCODE_RALT:
            return KEY_RALT;

        default:
            if (scancode >= 0 && scancode < arrlen(scancode_translate_table))
            {
                return scancode_translate_table[scancode];
            }
            else
            {
                return 0;
            }
    }
}

// Get the localized version of the key press. This takes into account the
// keyboard layout, but does not apply any changes due to modifiers, (eg.
// shift-, alt-, etc.)
static int GetLocalizedKey(int scancode)
{
    // When using Vanilla mapping, we just base everything off the scancode
    // and always pretend the user is using a US layout keyboard.
    if (vanilla_keyboard_mapping)
    {
        return TranslateKey(scancode);
    }
    else
    {
        assert(false); return 0;
//        int result = sym->sym;
//
//        if (result < 0 || result >= 128)
//        {
//            result = 0;
//        }
//
//        return sym_<result;
    }
}

// Get the equivalent ASCII (Unicode?) character for a keypress.
int GetTypedChar(int scancode, boolean shiftdown)
{
    // We only return typed characters when entering text, after
    // I_StartTextInput() has been called. Otherwise we return nothing.
    if (!text_input_enabled)
    {
        return 0;
    }

    // If we're strictly emulating Vanilla, we should always act like
    // we're using a US layout keyboard (in ev_keydown, data1=data2).
    // Otherwise we should use the native key mapping.
    if (vanilla_keyboard_mapping)
    {
        int result = TranslateKey(scancode);

        // If shift is held down, apply the original uppercase
        // translation table used under DOS.
        if (shiftdown
            && result >= 0 && result < arrlen(shiftxform))
        {
            result = shiftxform[result];
        }

        return result;
    }
    else
    {
#if 0
        SDL_Event next_event;

        // Special cases, where we always return a fixed value.
        switch (sym->sym)
        {
            case SDLK_BACKSPACE: return KEY_BACKSPACE;
            case SDLK_RETURN:    return KEY_ENTER;
            default:
                break;
        }

        // The following is a gross hack, but I don't see an easier way
        // of doing this within the SDL2 API (in SDL1 it was easier).
        // We want to get the fully transformed input character associated
        // with this keypress - correct keyboard layout, appropriately
        // transformed by any modifier keys, etc. So peek ahead in the SDL
        // event queue and see if the key press is immediately followed by
        // an SDL_TEXTINPUT event. If it is, it's reasonable to assume the
        // key press and the text input are connected. Technically the SDL
        // API does not guarantee anything of the sort, but in practice this
        // is what happens and I've verified it through manual inspect of
        // the SDL source code.
        //
        // In an ideal world we'd split out ev_keydown into a separate
        // ev_textinput event, as SDL2 has done. But this doesn't work
        // (I experimented with the idea), because lots of Doom's code is
        // based around different responders "eating" events to stop them
        // being passed on to another responder. If code is listening for
        // a text input, it cannot block the corresponding keydown events
        // which can affect other responders.
        //
        // So we're stuck with this as a rather fragile alternative.

        if (SDL_PeepEvents(&next_event, 1, SDL_PEEKEVENT,
                           SDL_FIRSTEVENT, SDL_LASTEVENT) == 1
            && next_event.type == SDL_TEXTINPUT)
        {
            // If an SDL_TEXTINPUT event is found, we always assume it
            // matches the key press. The input text must be a single
            // ASCII character - if it isn't, it's possible the input
            // char is a Unicode value instead; better to send a null
            // character than the unshifted key.
            if (strlen(next_event.text.text) == 1
                && (next_event.text.text[0] & 0x80) == 0)
            {
                return next_event.text.text[0];
            }
        }
#else
        assert(false);
#endif

        // Failed to find anything :/
        return 0;
    }
}

void I_StartTextInput(int x1, int y1, int x2, int y2)
{
    text_input_enabled = true;

    if (!vanilla_keyboard_mapping)
    {
#if !USE_VANILLA_KEYBOARD_MAPPING_ONLY
        // SDL2-TODO: SDL_SetTextInputRect(...);
        SDL_StartTextInput();
#endif
    }
}

void I_StopTextInput(void)
{
    text_input_enabled = false;

    if (!vanilla_keyboard_mapping)
    {
#if !USE_VANILLA_KEYBOARD_MAPPING_ONLY
        SDL_StopTextInput();
#endif
    }
}

#if !NO_USE_MOUSE
static void UpdateMouseButtonState(unsigned int button, boolean on)
{
    static event_t event;

    if (button < SDL_BUTTON_LEFT || button > MAX_MOUSE_BUTTONS)
    {
        return;
    }

    // Note: button "0" is left, button "1" is right,
    // button "2" is middle for Doom.  This is different
    // to how SDL sees things.

    switch (button)
    {
        case SDL_BUTTON_LEFT:
            button = 0;
            break;

        case SDL_BUTTON_RIGHT:
            button = 1;
            break;

        case SDL_BUTTON_MIDDLE:
            button = 2;
            break;

        default:
            // SDL buttons are indexed from 1.
            --button;
            break;
    }

    // Turn bit representing this button on or off.

    if (on)
    {
        mouse_button_state |= (1 << button);
    }
    else
    {
        mouse_button_state &= ~(1 << button);
    }

    // Post an event with the new button state.

    event.type = ev_mouse;
    event.data1 = mouse_button_state;
    event.data2 = event.data3 = 0;
    D_PostEvent(&event);
}

static void MapMouseWheelToButtons(SDL_MouseWheelEvent *wheel)
{
    // SDL2 distinguishes button events from mouse wheel events.
    // We want to treat the mouse wheel as two buttons, as per
    // SDL1
    static event_t up, down;
    int button;

    if (wheel->y <= 0)
    {   // scroll down
        button = 4;
    }
    else
    {   // scroll up
        button = 3;
    }

    // post a button down event
    mouse_button_state |= (1 << button);
    down.type = ev_mouse;
    down.data1 = mouse_button_state;
    down.data2 = down.data3 = 0;
    D_PostEvent(&down);

    // post a button up event
    mouse_button_state &= ~(1 << button);
    up.type = ev_mouse;
    up.data1 = mouse_button_state;
    up.data2 = up.data3 = 0;
    D_PostEvent(&up);
}

void I_HandleMouseEvent(SDL_Event *sdlevent)
{
    switch (sdlevent->type)
    {
        case SDL_MOUSEBUTTONDOWN:
            UpdateMouseButtonState(sdlevent->button.button, true);
            break;

        case SDL_MOUSEBUTTONUP:
            UpdateMouseButtonState(sdlevent->button.button, false);
            break;

        case SDL_MOUSEWHEEL:
            MapMouseWheelToButtons(&(sdlevent->wheel));
            break;

        default:
            break;
    }
}

static int AccelerateMouse(int val)
{
    if (val < 0)
        return -AccelerateMouse(-val);

    if (val > mouse_threshold)
    {
        return (int)((val - mouse_threshold) * mouse_acceleration + mouse_threshold);
    }
    else
    {
        return val;
    }
}

//
// Read the change in mouse state to generate mouse motion events
//
// This is to combine all mouse movement for a tic into one mouse
// motion event.
void I_ReadMouse(void)
{
    int x, y;
    event_t ev;

    SDL_GetRelativeMouseState(&x, &y);

    if (x != 0 || y != 0)
    {
        ev.type = ev_mouse;
        ev.data1 = mouse_button_state;
        ev.data2 = AccelerateMouse(x);

        if (!novert)
        {
            ev.data3 = -AccelerateMouse(y);
        }
        else
        {
            ev.data3 = 0;
        }

        // XXX: undefined behaviour since event is scoped to
        // this function
        D_PostEvent(&ev);
    }
}
#endif

// Bind all variables controlling input options.
void I_BindInputVariables(void)
{
#if !NO_USE_MOUSE
    M_BindFloatVariable("mouse_acceleration",      &mouse_acceleration);
    M_BindIntVariable("mouse_threshold",           &mouse_threshold);
#endif
#if !USE_VANILLA_KEYBOARD_MAPPING_ONLY
    M_BindIntVariable("vanilla_keyboard_mapping",  &vanilla_keyboard_mapping);
#endif
    M_BindIntVariable("novert",                    &novert);
}

#if PICO_NO_HARDWARE
#include "pico/scanvideo.h"
#else
#define WITH_SHIFT 0x8000
#endif

static void pico_key_down(int scancode, int keysym, int modifiers) {
    event_t event;
    event.type = ev_keydown;
    event.data1 = TranslateKey(scancode);
    event.data2 = GetLocalizedKey(scancode);
    event.data3 = GetTypedChar(scancode, modifiers & WITH_SHIFT ? 1 : 0);

    if (at_exit_screen) {
        handle_exit_key_down(scancode, modifiers & WITH_SHIFT ? 1 : 0, exit_screen_kb_buffer_80, 80);
        return;
    }
    if (event.data1 != 0)
    {
        D_PostEvent(&event);
    }
}

static void pico_key_up(int scancode, int keysym, int modifiers) {
    event_t event;
    event.type = ev_keyup;
    event.data1 = TranslateKey(scancode);
    // data2/data3 are initialized to zero for ev_keyup.
    // For ev_keydown it's the shifted Unicode character
    // that was typed, but if something wants to detect
    // key releases it should do so based on data1
    // (key ID), not the printable char.
    event.data2 = 0;
    event.data3 = 0;
    if (event.data1 != 0)
    {
        D_PostEvent(&event);
    }
}

#if PICO_NO_HARDWARE
static void pico_quit(void) {
    exit(0);
}
#endif

void I_InputInit(void) {
#if PICO_NO_HARDWARE
    platform_key_down = pico_key_down;
    platform_key_up = pico_key_up;
    platform_quit = pico_quit;
#elif USB_SUPPORT
    tusb_init();
    irq_set_priority(USBCTRL_IRQ, 0xc0);
#endif

#if ACCELEROMETER_SUPPORT
    accelerometer_init();
#endif

#if PIO_CAPSENSE
    capsense_init();
#endif

#if GPIO_BUTTONS
    buttons_init();
#endif


}

void I_GetEvent() {
#if USB_SUPPORT
    tuh_task();
#endif
    return I_GetEventTimeout(50);
}

void I_GetEventTimeout(int key_timeout) {

#if ACCELEROMETER_SUPPORT
    accelerometer_getevent();
#endif

#if PIO_CAPSENSE
    capsense_getevent();
#endif

#if GPIO_BUTTONS
    buttons_getevent();
#endif


#if PICO_ON_DEVICE && !NO_USE_UART
    if (uart_is_readable(uart_default)) {
        char c = uart_getc(uart_default);
        if (c == 26 && uart_is_readable_within_us(uart_default, key_timeout)) {
            c = uart_getc(uart_default);
            static int modifiers = 0;
            switch (c) {
                case 0:
                    if (uart_is_readable_within_us(uart_default, key_timeout)) {
                        uint scancode = (uint8_t) uart_getc(uart_default);
                        if (scancode == SDL_SCANCODE_LSHIFT || scancode == SDL_SCANCODE_RSHIFT) {
                            modifiers |= WITH_SHIFT;
                        }
                        pico_key_down(scancode, 0, modifiers);
                    }
                    return;
                case 1:
                    if (uart_is_readable_within_us(uart_default, key_timeout)) {
                        uint scancode = (uint8_t) uart_getc(uart_default);
                        if (scancode == SDL_SCANCODE_LSHIFT || scancode == SDL_SCANCODE_RSHIFT) {
                            modifiers &= ~WITH_SHIFT;
                        }
                        pico_key_up(scancode, 0, modifiers);
                    }
                    return;
                case 2:
                case 3:
                case 5:
                    if (uart_is_readable_within_us(uart_default, key_timeout)) {
                        uint __unused scancode = (uint8_t) uart_getc(uart_default);
                    }
                    return;
                case 4:
                    if (uart_is_readable_within_us(uart_default, key_timeout)) {
                        uint __unused scancode = (uint8_t) uart_getc(uart_default);
                    }
                    if (uart_is_readable_within_us(uart_default, key_timeout)) {
                        uint __unused scancode = (uint8_t) uart_getc(uart_default);
                    }
                    return;
            }
        }
    }
#endif
}

#if USB_SUPPORT

#define MAX_REPORT  4
#define debug_printf(fmt,...) ((void)0)

// Each HID instance can has multiple reports
static struct
{
    uint8_t report_count;
    tuh_hid_report_info_t report_info[MAX_REPORT];
}hid_info[CFG_TUH_HID];

static void process_kbd_report(hid_keyboard_report_t const *report);
static void process_mouse_report(hid_mouse_report_t const * report);
static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
    debug_printf("HID device address = %d, instance = %d is mounted\r\n", dev_addr, instance);

    // Interface protocol (hid_interface_protocol_enum_t)
    const char* protocol_str[] = { "None", "Keyboard", "Mouse" };
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    debug_printf("HID Interface Protocol = %s\r\n", protocol_str[itf_protocol]);
//    printf("%d USB: device %d connected, protocol %s\n", time_us_32() - t0 , dev_addr, protocol_str[itf_protocol]);

    // By default host stack will use activate boot protocol on supported interface.
    // Therefore for this simple example, we only need to parse generic report descriptor (with built-in parser)
    if ( itf_protocol == HID_ITF_PROTOCOL_NONE )
    {
        hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
        debug_printf("HID has %u reports \r\n", hid_info[instance].report_count);
    }

    // request to receive report
    // tuh_hid_report_received_cb() will be invoked when report is available
    if ( !tuh_hid_receive_report(dev_addr, instance) )
    {
        debug_printf("Error: cannot request to receive report\r\n");
    }
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    debug_printf("HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
    printf("USB: device %d disconnected\n", dev_addr);
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    switch (itf_protocol)
    {
        case HID_ITF_PROTOCOL_KEYBOARD:
            TU_LOG2("HID receive boot keyboard report\r\n");
            process_kbd_report( (hid_keyboard_report_t const*) report );
            break;

#if !NO_USE_MOUSE
            case HID_ITF_PROTOCOL_MOUSE:
      TU_LOG2("HID receive boot mouse report\r\n");
      process_mouse_report( (hid_mouse_report_t const*) report );
    break;
#endif

        default:
            // Generic report requires matching ReportID and contents with previous parsed report info
            process_generic_report(dev_addr, instance, report, len);
            break;
    }

    // continue to request to receive report
    if ( !tuh_hid_receive_report(dev_addr, instance) )
    {
        debug_printf("Error: cannot request to receive report\r\n");
    }
}

//--------------------------------------------------------------------+
// Keyboard
//--------------------------------------------------------------------+

// look up new key in previous keys
static inline bool find_key_in_report(hid_keyboard_report_t const *report, uint8_t keycode)
{
    for(uint8_t i=0; i<6; i++)
    {
        if (report->keycode[i] == keycode)  return true;
    }

    return false;
}

static void check_mod(int mod, int prev_mod, int mask, int scancode) {
    if ((mod^prev_mod)&mask) {
        if (mod & mask)
            pico_key_down(scancode, 0, 0);
        else
            pico_key_up(scancode, 0, 0);
    }
}

static void process_kbd_report(hid_keyboard_report_t const *report)
{
    static hid_keyboard_report_t prev_report = { 0, 0, {0} }; // previous report to check key released

    //------------- example code ignore control (non-printable) key affects -------------//
    for(uint8_t i=0; i<6; i++)
    {
        if ( report->keycode[i] )
        {
            if ( find_key_in_report(&prev_report, report->keycode[i]) )
            {
                // exist in previous report means the current key is holding
            }else
            {
                // not existed in previous report means the current key is pressed
                bool const is_shift = report->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
                pico_key_down(report->keycode[i], 0, is_shift ? WITH_SHIFT : 0);
            }
        }
        // Check for key depresses (i.e. was present in prev report but not here)
        if (prev_report.keycode[i]) {
            // If not present in the current report then depressed
            if (!find_key_in_report(report, prev_report.keycode[i]))
            {
                bool const is_shift = report->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
                pico_key_up(prev_report.keycode[i], 0, is_shift ? WITH_SHIFT : 0);
            }
        }
    }
    // synthesize events for modifier keys
    static const uint8_t mods[] = {
            KEYBOARD_MODIFIER_LEFTCTRL, SDL_SCANCODE_LCTRL,
            KEYBOARD_MODIFIER_RIGHTCTRL, SDL_SCANCODE_RCTRL,
            KEYBOARD_MODIFIER_LEFTALT, SDL_SCANCODE_LALT,
            KEYBOARD_MODIFIER_RIGHTALT, SDL_SCANCODE_RALT,
            KEYBOARD_MODIFIER_LEFTSHIFT, SDL_SCANCODE_LSHIFT,
            KEYBOARD_MODIFIER_RIGHTSHIFT, SDL_SCANCODE_RSHIFT,
    };
    for(int i=0;i<count_of(mods); i+= 2) {
        check_mod(report->modifier, prev_report.modifier, mods[i], mods[i+1]);
    }
    prev_report = *report;
}

//--------------------------------------------------------------------+
// Mouse
//--------------------------------------------------------------------+

#if !NO_USE_MOUSE
static void process_mouse_report(hid_mouse_report_t const * report)
{
    static hid_mouse_report_t prev_report = { 0 };

    uint8_t button_changed_mask = report->buttons ^ prev_report.buttons;
    if ( button_changed_mask & report->buttons)
    {
        debug_printf(" %c%c%c ",
                     report->buttons & MOUSE_BUTTON_LEFT   ? 'L' : '-',
                     report->buttons & MOUSE_BUTTON_MIDDLE ? 'M' : '-',
                     report->buttons & MOUSE_BUTTON_RIGHT  ? 'R' : '-');
    }

//    cursor_movement(report->x, report->y, report->wheel);
}
#endif

//--------------------------------------------------------------------+
// Generic Report
//--------------------------------------------------------------------+
static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
    (void) dev_addr;

    uint8_t const rpt_count = hid_info[instance].report_count;
    tuh_hid_report_info_t* rpt_info_arr = hid_info[instance].report_info;
    tuh_hid_report_info_t* rpt_info = NULL;

    if ( rpt_count == 1 && rpt_info_arr[0].report_id == 0)
    {
        // Simple report without report ID as 1st byte
        rpt_info = &rpt_info_arr[0];
    }else
    {
        // Composite report, 1st byte is report ID, data starts from 2nd byte
        uint8_t const rpt_id = report[0];

        // Find report id in the arrray
        for(uint8_t i=0; i<rpt_count; i++)
        {
            if (rpt_id == rpt_info_arr[i].report_id )
            {
                rpt_info = &rpt_info_arr[i];
                break;
            }
        }

        report++;
        len--;
    }

    if (!rpt_info)
    {
        debug_printf("Couldn't find the report info for this report !\r\n");
        return;
    }

    // For complete list of Usage Page & Usage checkout src/class/hid/hid.h. For examples:
    // - Keyboard                     : Desktop, Keyboard
    // - Mouse                        : Desktop, Mouse
    // - Gamepad                      : Desktop, Gamepad
    // - Consumer Control (Media Key) : Consumer, Consumer Control
    // - System Control (Power key)   : Desktop, System Control
    // - Generic (vendor)             : 0xFFxx, xx
    if ( rpt_info->usage_page == HID_USAGE_PAGE_DESKTOP )
    {
        switch (rpt_info->usage)
        {
            case HID_USAGE_DESKTOP_KEYBOARD:
                TU_LOG1("HID receive keyboard report\r\n");
                // Assume keyboard follow boot report layout
                process_kbd_report( (hid_keyboard_report_t const*) report );
                break;

#if !NO_USE_MOUSE
                case HID_USAGE_DESKTOP_MOUSE:
        TU_LOG1("HID receive mouse report\r\n");
        // Assume mouse follow boot report layout
        process_mouse_report( (hid_mouse_report_t const*) report );
      break;
#endif

            default: break;
        }
    }
}

#endif