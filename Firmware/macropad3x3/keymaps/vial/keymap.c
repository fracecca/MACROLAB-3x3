#include QMK_KEYBOARD_H
#include <stdbool.h>
#include <stdint.h>

// --- Fix per Vial: Array reali con elementi dummy ---
#if defined(COMBO_ENABLE)
const uint16_t PROGMEM dummy_combo[] = {KC_NO, COMBO_END};
combo_t key_combos[] = { COMBO(dummy_combo, KC_NO) };
#endif

#if defined(TAP_DANCE_ENABLE)
tap_dance_action_t tap_dance_actions[] = {
    [0] = ACTION_TAP_DANCE_DOUBLE(KC_NO, KC_NO)
};
#endif

#if defined(KEY_OVERRIDE_ENABLE)
const key_override_t dummy_override = ko_make_basic(MOD_MASK_SHIFT, KC_NO, KC_NO);
const key_override_t *key_overrides[] = { &dummy_override };
#endif
// ----------------------------------------------------

// --- PIN HARDWARE ---
#define ENCODER_SW_PIN GP4
#define SDA_PIN GP0        
#define SCL_PIN GP1        

enum layers { _L0 = 0, _L1, _L2, _L3 };

// --- MATRICE 3x3 PULITA ---
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_L0] = LAYOUT(KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7, KC_8, KC_9),
    [_L1] = LAYOUT(KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS),
    [_L2] = LAYOUT(KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS),
    [_L3] = LAYOUT(KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS)
};

// --- MAPPA ROTELLA ENCODER PER VIAL ---
#if defined(ENCODER_MAP_ENABLE)
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][2] = {
    [_L0] = { ENCODER_CCW_CW(KC_VOLD, KC_VOLU) },
    [_L1] = { ENCODER_CCW_CW(KC_TRNS, KC_TRNS) },
    [_L2] = { ENCODER_CCW_CW(KC_TRNS, KC_TRNS) },
    [_L3] = { ENCODER_CCW_CW(KC_TRNS, KC_TRNS) }
};
#endif

// --- VARIABILI DI STATO ---
uint8_t last_layer = 255;
uint32_t encoder_timer = 0;
bool encoder_cw = false;
bool last_encoder_active = false;
bool last_encoder_cw = false;

uint32_t key_timer = 0;
bool last_typing_active = false;

// --- MOTORE SCHERMO OLED I2C BIT-BANG ---
static void i2c_delay(void) { for (int i = 0; i < 50; i++) __asm("nop"); }
static void i2c_start(void) { writePinHigh(SDA_PIN); writePinHigh(SCL_PIN); i2c_delay(); writePinLow(SDA_PIN); i2c_delay(); writePinLow(SCL_PIN); }
static void i2c_stop(void) { writePinLow(SDA_PIN); writePinLow(SCL_PIN); i2c_delay(); writePinHigh(SCL_PIN); i2c_delay(); writePinHigh(SDA_PIN); }
static void i2c_write_byte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        if (byte & (1 << i)) writePinHigh(SDA_PIN); else writePinLow(SDA_PIN);
        i2c_delay(); writePinHigh(SCL_PIN); i2c_delay(); i2c_delay(); writePinLow(SCL_PIN);
    }
    writePinHigh(SDA_PIN); i2c_delay(); writePinHigh(SCL_PIN); i2c_delay(); writePinLow(SCL_PIN);
}
static void send_oled_cmd(uint8_t cmd) { i2c_start(); i2c_write_byte(0x3C << 1); i2c_write_byte(0x00); i2c_write_byte(cmd); i2c_stop(); }
static void send_oled_data(uint8_t *data, uint16_t size) {
    i2c_start(); i2c_write_byte(0x3C << 1); i2c_write_byte(0x40);
    for (uint16_t i = 0; i < size; i++) i2c_write_byte(data[i]);
    i2c_stop();
}

static const uint8_t font_chars[4][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // Spazio
    {0x08, 0x14, 0x22, 0x41, 0x00}, // Freccia '<'
    {0x41, 0x22, 0x14, 0x08, 0x00}, // Freccia '>'
    {0x1C, 0x3E, 0x3E, 0x3E, 0x1C}  // Pallino '*'
};

static const uint8_t hd_numbers[4][24] = {
    { 0x00,0x00,0x00,0x04,0x06,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00 },
    { 0x83,0x83,0x83,0x83,0x83,0x83,0x83,0x83,0x83,0x83,0xFF,0xFF, 0xFF,0xFF,0xC1,0xC1,0xC1,0xC1,0xC1,0xC1,0xC1,0xC1,0xC1,0xC1 },
    { 0x83,0x83,0x83,0x83,0x83,0x83,0x83,0x83,0x83,0x83,0xFF,0xFF, 0xC1,0xC1,0xC1,0xC1,0xC1,0xC1,0xC1,0xC1,0xC1,0xC1,0xFF,0xFF },
    { 0xFF,0xFF,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0xFF,0xFF, 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0xFF,0xFF }
};

static void draw_string_centered(uint8_t x, uint8_t page_y, uint8_t font_idx, uint8_t cell_w) {
    uint8_t offset = (cell_w - 5) / 2;
    uint8_t curr_x = x + offset;
    send_oled_cmd(0xB0 + page_y);
    send_oled_cmd(curr_x & 0x0F);
    send_oled_cmd(0x10 + ((curr_x >> 4) & 0x0F));
    send_oled_data((uint8_t*)font_chars[font_idx], 5);
}

static void clear_oled(void) {
    for (uint8_t page = 0; page < 8; page++) {
        send_oled_cmd(0xB0 + page); send_oled_cmd(0x00); send_oled_cmd(0x10);
        uint8_t zeros[128] = {0}; send_oled_data(zeros, 128);
    }
}

void draw_big_number_box(uint8_t x, uint8_t page_y, uint8_t num_index, bool active) {
    uint8_t col_data[3][24] = {{0}}; 
    for(int i=0; i<24; i++) { col_data[0][i] = 0x01; col_data[2][i] = 0x80; }
    for(int p=0; p<3; p++) { col_data[p][0] = 0xFF; col_data[p][23] = 0xFF; }
    col_data[0][0] = 0xFE; col_data[0][23] = 0xFE; col_data[2][0] = 0x7F; col_data[2][23] = 0x7F; 

    uint8_t start_x = 6; 
    for(int c=0; c<12; c++) {
        uint32_t col = (hd_numbers[num_index][c + 12] << 8) | hd_numbers[num_index][c];
        col <<= 4;
        col_data[0][start_x + c] |= (col & 0xFF);
        col_data[1][start_x + c] |= ((col >> 8) & 0xFF);
        col_data[2][start_x + c] |= ((col >> 16) & 0xFF);
    }

    if (active) {
        for(int p=0; p<3; p++) { for(int i=0; i<24; i++) { col_data[p][i] = ~col_data[p][i]; } }
    }

    for(int p=0; p<3; p++) {
        send_oled_cmd(0xB0 + page_y + p);
        send_oled_cmd(x & 0x0F); send_oled_cmd(0x10 + ((x >> 4) & 0x0F));
        send_oled_data(col_data[p], 24);
    }
}

void draw_ui(void) {
    uint8_t current_layer = get_highest_layer(layer_state) % 4;
    // Frecce a 1500 millisecondi (1.5 secondi)
    bool encoder_active = (timer_elapsed32(encoder_timer) < 1500);
    bool typing_active = (timer_elapsed32(key_timer) < 150);

    draw_big_number_box(4,   1, 0, current_layer == 0);
    draw_big_number_box(36,  1, 1, current_layer == 1);
    draw_big_number_box(68,  1, 2, current_layer == 2);
    draw_big_number_box(100, 1, 3, current_layer == 3);

    send_oled_cmd(0xB0 + 6); send_oled_cmd(0x00); send_oled_cmd(0x10);
    uint8_t empty[128] = {0}; send_oled_data(empty, 128);

    // Frecce encoder
    if (encoder_active) {
        if (encoder_cw) { for(int i=0; i<4; i++) draw_string_centered(88 + (i*6), 6, 2, 6); }
        else { for(int i=0; i<4; i++) draw_string_centered(16 + (i*6), 6, 1, 6); }
    }

    // Pallino digitazione
    if (typing_active) {
        draw_string_centered(0, 6, 3, 128); 
    }
}

// --- HOOKS DI SISTEMA ---
void keyboard_post_init_user(void) {
    wait_ms(200);
    setPinOutput(SDA_PIN); setPinOutput(SCL_PIN);
    writePinHigh(SDA_PIN); writePinHigh(SCL_PIN);
    
    // Imposta il pin del pulsante encoder come input con pull-up
    setPinInputHigh(ENCODER_SW_PIN); 
    
    uint8_t init_cmds[] = {0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0x2E, 0xAF};
    for(int i=0; i<sizeof(init_cmds); i++) send_oled_cmd(init_cmds[i]);

    clear_oled(); 
    last_layer = 255; 
    
    // Spostiamo i timer "nel passato" per non far apparire le frecce all'avvio
    encoder_timer = timer_read32() - 2000;
    key_timer = timer_read32() - 2000;
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        // Se le coordinate appartengono alla vera matrice fisica 3x3
        if (record->event.key.row < 3 && record->event.key.col < 3) {
            key_timer = timer_read32(); // Tasto vero: Accende il PALLINO
        } else {
            // Se è fuori matrice, è il finto tasto generato dall'encoder di Vial!
            encoder_timer = timer_read32(); // Rotella: Accende le FRECCE
            
            // Decidiamo il senso delle frecce (<<< o >>>) in base al comando inviato
            if (keycode == KC_VOLU || keycode == KC_RIGHT || keycode == KC_DOWN || keycode == KC_PGDN) {
                encoder_cw = true;
            } else if (keycode == KC_VOLD || keycode == KC_LEFT || keycode == KC_UP || keycode == KC_PGUP) {
                encoder_cw = false;
            } else {
                encoder_cw = !encoder_cw; // Alterna se assegni tasti strani
            }
        }
    }
    return true; 
}

// Funzione encoder sempre attiva
bool encoder_update_user(uint8_t index, bool clockwise) {
    encoder_timer = timer_read32();
    encoder_cw = clockwise;
    return true; 
}

void matrix_scan_user(void) {
    static bool last_pin_state = true; 
    static uint32_t press_timer = 0;
    static bool layer_changed = false;

    // Lettura manuale pulsante Encoder (Cambio Layer ciclico)
    bool current_pin_state = readPin(ENCODER_SW_PIN); 
    if (current_pin_state != last_pin_state) {
        last_pin_state = current_pin_state;
        if (!current_pin_state) { 
            press_timer = timer_read32(); 
            layer_changed = false;   
            key_timer = timer_read32(); // Accende il pallino
        }
    }

    if (!current_pin_state && !layer_changed && timer_elapsed32(press_timer) >= 300) {
        uint8_t layer = get_highest_layer(layer_state);
        layer_move((layer + 1) % 4); 
        layer_changed = true; 
    }

    // Aggiornamento interfaccia OLED
    uint8_t current_layer = get_highest_layer(layer_state) % 4;
    // Frecce a 1500 millisecondi (1.5 secondi)
    bool encoder_active = (timer_elapsed32(encoder_timer) < 1500);
    bool typing_active = (timer_elapsed32(key_timer) < 150);

    if (current_layer != last_layer || encoder_active != last_encoder_active || 
        (encoder_active && encoder_cw != last_encoder_cw) || typing_active != last_typing_active) {
        draw_ui(); 
        last_layer = current_layer;
        last_encoder_active = encoder_active;
        last_encoder_cw = encoder_cw;
        last_typing_active = typing_active;
    }
}