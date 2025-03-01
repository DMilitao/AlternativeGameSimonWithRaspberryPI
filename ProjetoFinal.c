/* Projeto Final Capacitação Embarcatech
Douglas Lima Militão Pinheiro
Turma: Chipflow

    Módulos ensinados | Utilizados no projeto?
        GPIO                S
        Interrupções        S
        TIMER               S
        PWM                 S
        ADC                 S
        Comunicação(i2c)    S

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "hardware/timer.h"
#include "hardware/i2c.h"
#include "inc/ssd1306.h"


// Definições de constantes para pinos e parâmetros
#define JS_x 27
#define CHAN_JS_x 1
#define JS_y 26
#define CHAN_JS_y 0
#define JS_sw 22
#define LEDvm 13
#define LEDaz 12
#define LEDvd 11
#define BTN_B 6
#define BTN_A 5
#define BUZZER_PIN 21

// Frequência do som para cada caso de LED e aviso
#define FREQvd 400
#define FREQaz 500
#define FREQvm 600
#define FREQam 700
#define FREQ_Aviso 1200
#define FREQ_Jogo_Perdido 200
#define FREQ_Inicio_Jogo 2000
#define FREQ_Jogo_Ganho 1000

// Valores limites para definir uma cor selecionada no joystick
#define Threshold_max 3000
#define Threshold_min 1000

// Valores para contagem do aviso
#define CONT_T_BASE 3000    
#define NUM_AVISOS 10       // Num de avisos+1

// Constantes para PWM do LED (Não acionado por GPIO diretamente para melhorar conforto aos olhos)
#define PWM_FREQ 1000       
#define PWM_DIV 4.0f         

// Constante para o Buzzer
#define PWM_DIV_BUZZER 16.0f

// Quantidade de níveis no jogo
#define NUM_LEVELS 15       
#define CONT_LEVEL_BASE 5000

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15

// Variáveis globais
uint16_t adc_y_raw, adc_x_raw;  // Leituras do joystick

uint8_t sequencia[NUM_LEVELS];  // Sequência de cores para o jogo
uint8_t level = 1;              // Nível do usuário
uint clock_sys, ticks_LEDs;     // Variáveis auxiliares para gerenciamento dos PWMs
int led_atual = 0, led_prev = 0;// Guardar seleção do LED atual e a precedente
int cont_tempo_joystick = 1;    // Variável auxiliar para contagem do tempo de seleção
bool flag_press = 0, flag_aviso = 0, flag_init = 0, flag_jogo_perdido = 0; // Flags auxiliares para gerenciamento do código

// Variáveis auxiliares para alternar brilho dos LEDs
float dc_leds[2] = {0.1, 0.9};
uint8_t dc_value = 0;

// Variáveis para os timers criados
alarm_id_t timer_joystick, timer_buzzer; 

// Variável OLED
struct render_area frame_area = {
        start_column : 0,
        end_column : ssd1306_width - 1,
        start_page : 0,
        end_page : ssd1306_n_pages - 1
    };

// Inicializa PWM para o pino informado
void init_pwm(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(pin);
    uint32_t clock_freq = clock_get_hz(clk_sys);

    pwm_config config = pwm_get_default_config();
    uint32_t ticks;
    if(pin == BUZZER_PIN){ // Verifica se pino é do BUZZER para configurar clock_div
        ticks = (clock_freq / (PWM_FREQ * PWM_DIV_BUZZER)) - 1;
        pwm_config_set_clkdiv(&config, PWM_DIV_BUZZER); 
    }else{
        ticks = (clock_freq / (PWM_FREQ * PWM_DIV)) - 1;
        pwm_config_set_clkdiv(&config, PWM_DIV); 
    }
    
    pwm_set_wrap(slice_num, ticks);             
    pwm_init(slice_num, &config, true);         
    pwm_set_gpio_level(pin, 0);                 
}

// Configura o nível PWM para os LEDs
void set_pwm_level(uint pin, float duty_cycle) {
    uint slice_num = pwm_gpio_to_slice_num(pin);
    pwm_set_wrap(slice_num, ticks_LEDs);
    pwm_set_gpio_level(pin, (uint)(duty_cycle * ticks_LEDs));  // Define o nível PWM
}

// Interrupção do timer durante seleção do joystick para contar passagem do tempo e enviar flags aos códigos
bool timer_callback_joystick() {
    cont_tempo_joystick++;

    if(cont_tempo_joystick <= NUM_AVISOS){
        flag_aviso = 1;
    }else{
        flag_jogo_perdido = 1;
    }
    return false;
}

// Interrupção do timer para desligar o buzzer
bool timer_callback_buzzer() {
    pwm_set_gpio_level(BUZZER_PIN, 0);
    return false; 
}

// Função para tocar o buzzer com PWM (volume constante em 50% independente do tom)
void tocar(uint pin, uint frequencia, uint dur_ms) {
    uint slice_num = pwm_gpio_to_slice_num(pin);
    uint ticks = clock_sys / (frequencia*PWM_DIV_BUZZER) - 1;

    pwm_set_wrap(slice_num, ticks);
    pwm_set_gpio_level(pin, ticks/2);
    // Cria alarme para desativar o buzzer após duração determinada
    cancel_alarm(timer_buzzer);
    // Cria interrupção do timer para desativar o buzzer
    timer_buzzer = add_alarm_in_ms(dur_ms, (alarm_callback_t)timer_callback_buzzer, NULL, false);
}

// Gera sequência aleatória para o jogo
void gera_sequencia(){
    for (int i = 0; i < sizeof(sequencia); i++) {
        sequencia[i] = rand() % 4 + 1;
    }
}

// Interrupção do botão para atualizar estado do jogo
void interrup_btn(int gpio, uint32_t eventos){
    if (gpio == BTN_B && led_atual != 0 && flag_init) { // Verifica se o Botão B foi ativado e se alguma cor foi selecionada
        flag_press = 1;
        gpio_set_irq_enabled(BTN_B, GPIO_IRQ_EDGE_FALL, false);
    } else if(gpio == BTN_B){ // Inicializa um jogo quado o botão pressionado
        flag_init = 1;
    }

    if(gpio == BTN_A){ // Muda o valor do duty cycle que aciona os LEDs
        dc_value++;
        if(dc_value>((sizeof(dc_leds)/sizeof(dc_leds[0]))-1)){dc_value = 0;}
    }
}

// Atualiza os LEDs que devem ser acesos
void atualiza_led(uint valor){
    // Desativa todos os LED inicialmente
    set_pwm_level(LEDvm, 0);
    set_pwm_level(LEDvd, 0);
    set_pwm_level(LEDaz, 0);
    
    // Acende apenas o LED correspondente ao caso e toca o som correspondente ao LED
    switch (valor){
    case 1: // Vermelho
        set_pwm_level(LEDvm, dc_leds[dc_value]);
        tocar(BUZZER_PIN, FREQvm, 100);
        break;
    case 2: // Verde
        set_pwm_level(LEDvd, dc_leds[dc_value]);
        tocar(BUZZER_PIN, FREQvd,100);
        break;
    case 3: // Azul
        set_pwm_level(LEDaz, dc_leds[dc_value]);
        tocar(BUZZER_PIN, FREQaz,100);
        break;
    case 4: // Amarelo (vermelho + verde)
        set_pwm_level(LEDvm, dc_leds[dc_value]);
        set_pwm_level(LEDvd, dc_leds[dc_value]);
        tocar(BUZZER_PIN, FREQam,100);
        break;
    default: 
        break;
    }
}

// Lógica de leitura do joystick
void joystick_ativo() {
    led_prev = led_atual;

    // Lê valores do Joystick
    adc_select_input(CHAN_JS_y);
    adc_y_raw = adc_read();
    adc_select_input(CHAN_JS_x);
    adc_x_raw = adc_read();

    // Seleciona o LED atual (1 a 4) de acordo com valores do Joystick
    if (adc_y_raw > Threshold_max) { 
        led_atual = 1; 
    } else if (adc_x_raw > Threshold_max) { 
        led_atual = 3; 
    } else if (adc_y_raw < Threshold_min) { 
        led_atual = 2; 
    } else if (adc_x_raw < Threshold_min) { 
        led_atual = 4; 
    }

    // Atualiza brilho dos LEDs caso estado tenha mudado
    if (led_atual != led_prev) { 
        atualiza_led(led_atual); 
    }
}

void inicializa_jogo(){ // Acende os LEDs em sequência para indicar que está começando um novo jogo
    atualiza_led(0);
    sleep_ms(150);
    for(int i = 0; i < 2; i++){
            for(int j = 1; j <= 4; j++){
                atualiza_led(j);
                sleep_ms(150);
            }
    }
}

void limpa_tela(uint8_t ssd[]){
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);
}

void escreva(uint8_t ssd[], char *text[], size_t size_text){
    limpa_tela(ssd);
    int y = 0;
    for (uint i = 0; i < size_text; i++)
    {
        ssd1306_draw_string(ssd, 5, y, text[i]);
        y += 8;
    }
    render_on_display(ssd, &frame_area);
}

// Função principal
int main() {
    stdio_init_all();

    // Configura variáveis iniciais
    clock_sys = clock_get_hz(clk_sys);
    ticks_LEDs = (clock_sys / (PWM_FREQ * PWM_DIV)) - 1;

    // Inicializa ADC
    adc_init();
    adc_gpio_init(JS_x);
    adc_gpio_init(JS_y);

    // Inicializa PWM dos LEDs e do Buzzer
    init_pwm(LEDvm);
    init_pwm(LEDaz);
    init_pwm(LEDvd);
    init_pwm(BUZZER_PIN);

    // Inicializa Botão B
    gpio_init(BTN_B);
    gpio_set_dir(BTN_B, GPIO_IN);
    gpio_pull_up(BTN_B);

    // Inicializa Botão A
    gpio_init(BTN_A);
    gpio_set_dir(BTN_A, GPIO_IN);
    gpio_pull_up(BTN_A);
    gpio_set_irq_enabled_with_callback(BTN_A, GPIO_IRQ_EDGE_FALL, true, (gpio_irq_callback_t)interrup_btn);

    // Inicialização do i2c
    i2c_init(i2c1, ssd1306_i2c_clock * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Processo de inicialização completo do OLED SSD1306
    ssd1306_init();

    calculate_render_area_buffer_length(&frame_area);
    uint8_t ssd[ssd1306_buffer_length];

    limpa_tela(ssd);
    
    char *text[8];

    // Aloca memória para cada string e inicializa os valores
    text[0] = malloc(15);
    text[1] = malloc(15);
    text[2] = malloc(15);
    text[3] = malloc(15);
    text[4] = malloc(15);
    text[5] = malloc(15);
    text[6] = malloc(15);
    text[7] = malloc(15);


    // Inicializando as strings
    strcpy(text[0], " ");
    strcpy(text[1], " ");
    strcpy(text[2], " ");
    strcpy(text[3], " ");
    strcpy(text[4], " ");
    strcpy(text[5], " ");
    strcpy(text[6], " ");
    strcpy(text[7], " ");
    
    escreva(ssd, text, sizeof(text)/sizeof(text[0]));
// */


    // Gera a primeira sequência de numero aleatórios e reinicia flags
    gera_sequencia();
    flag_init = 0;
    flag_jogo_perdido = 0;

    while (true) {
        // Espera o botão ser pressionado para iniciar uma partida
        if(!flag_init){
            strcpy(text[0], "Bem vindo!!!");
            strcpy(text[2], "Aperte o botao");
            strcpy(text[4], "direito para");
            strcpy(text[6], "comecar");
            escreva(ssd, text, sizeof(text)/sizeof(text[0]));
            // Ativa interrupção do botão B
            gpio_set_irq_enabled_with_callback(BTN_B, GPIO_IRQ_EDGE_FALL, true, (gpio_irq_callback_t)interrup_btn);
            while(!flag_init){
                joystick_ativo();   // Ativa o joystick esperando o botão B ser pressionado para iniciar um jogo
            }
            // Desativa interrupção do botão B
            gpio_set_irq_enabled(BTN_B, GPIO_IRQ_EDGE_FALL, false);
            
            inicializa_jogo();
        }
        // Desliga todos os LEDs
        atualiza_led(0);
        sleep_ms(1000);
        tocar(BUZZER_PIN, FREQ_Inicio_Jogo, 300); // Sinal que indica que a sequência do level atual será apresentada
        
        strcpy(text[0], "Iniciando");
        strcpy(text[2], "");
        sprintf(text[4], "Level %d", level);
        strcpy(text[6], "");
        escreva(ssd, text, sizeof(text)/sizeof(text[0]));

        sleep_ms(1000);

        // Mostra a sequência para o usuário com velocidade crescente a cada level
        for (int i = 0; i < level; i++) {
            atualiza_led(sequencia[i]);
            sleep_ms(CONT_LEVEL_BASE - (level-1)*CONT_LEVEL_BASE/level);
        }
        // Desliga todos os LEDs, seta selecão atual para 0
        atualiza_led(0);
        led_atual = 0;
        flag_jogo_perdido = 0; // Jogo iniciado com o usuário ganhando inicialmente

        strcpy(text[0], "Selecione com");
        strcpy(text[2], "o joystick");
        strcpy(text[4], "Confirme com");
        strcpy(text[6], "o botao");
        escreva(ssd, text, sizeof(text)/sizeof(text[0]));

        // Entra em loop de acordo com o nível atual do usuário
        for (int i = 0; i < level; i++) {
            
            led_atual = 0;      // Reinicia seleção atual
            flag_press = 0;     // Reinicia flag do botão pressionado
            cont_tempo_joystick = 1;    // Reinicia contagem do tempo do joystick e habilita timer
            sleep_ms(200);
            timer_joystick = add_alarm_in_ms(CONT_T_BASE, (alarm_callback_t)timer_callback_joystick, NULL, false);
            gpio_set_irq_enabled_with_callback(BTN_B, GPIO_IRQ_EDGE_FALL, true, (gpio_irq_callback_t)interrup_btn);
            // Permanece em loop esperando um LED ser selecionado ou acabar o tempo e perder o jogo instantaneamente
            while (!flag_press && !flag_jogo_perdido) {
                joystick_ativo();   // Ativa o joystick para seleção do LED
                if(flag_aviso){     // Ativa aviso de passagem do tempo que diminui progressivamente
                    flag_aviso = 0;
                    tocar(BUZZER_PIN, FREQ_Aviso, 40);
                    // Contagem do tempo iniciou com CONT_T_BASE, depois CONT_T_BASE/2, depois CONT_T_BASE/3 até atingir o número de avisos
                    // e encerrar jogo
                    timer_joystick = add_alarm_in_ms(CONT_T_BASE/cont_tempo_joystick,(alarm_callback_t)timer_callback_joystick, NULL, false);
                }
            }
            // Desliga os LEDs e cancela qualquer timer de contagem de tempo joystick que ainda estiver ativo
            cancel_alarm(timer_joystick);
            atualiza_led(0);

            // Se usuário erra ou espera tempo demais para selecionar, perde o jogo e sai do loop de seleção
            if (led_atual != sequencia[i] || flag_jogo_perdido) {
                flag_jogo_perdido = 1;
                // Desativa interação com o botão
                gpio_set_irq_enabled(BTN_B, GPIO_IRQ_EDGE_FALL, false);
                break;
            }
        }
        
        
        // Se saiu do loop e flag continuou em zero, significa que acertou todas os LEDs em sequência
        // se não, significa que perdeu
        if (flag_jogo_perdido == 0) {
            // Adiciona um nível
            level++;
            // Se level é superior ao número de levels significa que o usuário ganhou o jogo
            // LED verde pisca 3 vezes, gera uma sequência nova e reinicia as flags do jogo,
            // se não apenas acende o LED verde durante alguns instantes e passa ao próximo level
            if (level > NUM_LEVELS) {
                strcpy(text[0], "Voce ganhoou");
                    strcpy(text[2], "");
                    strcpy(text[4], "");
                    strcpy(text[6], "");
                    escreva(ssd, text, sizeof(text)/sizeof(text[0]));
                for (int i = 0; i < 3; i++) {
                    set_pwm_level(LEDvd, dc_leds[dc_value]);
                    tocar(BUZZER_PIN, FREQ_Jogo_Ganho, 500);
                    sleep_ms(500);
                    set_pwm_level(LEDvd, 0);
                    sleep_ms(250);
                }
                gera_sequencia();
                level = 1;
                flag_init = 0;
                flag_jogo_perdido = 0;
            }else {
                strcpy(text[0], "Proxima rodada");
                strcpy(text[2], "");
                strcpy(text[4], "");
                strcpy(text[6], "");
                escreva(ssd, text, sizeof(text)/sizeof(text[0]));
                set_pwm_level(LEDvd, dc_leds[dc_value]);
                tocar(BUZZER_PIN, FREQ_Jogo_Ganho, 300);
                sleep_ms(1000);
                set_pwm_level(LEDvd, 0);
            }
        
        } else {
            // Acende LED vermelho, gera nova sequência e reinicia flags
            strcpy(text[0], "Voce perdeeu");
            strcpy(text[2], "");
            strcpy(text[4], "");
            strcpy(text[6], "");
            escreva(ssd, text, sizeof(text)/sizeof(text[0]));
            for (int i = 0; i < 3; i++) {
                set_pwm_level(LEDvm, dc_leds[dc_value]);
                tocar(BUZZER_PIN, FREQ_Jogo_Perdido, 500);
                sleep_ms(500);
                set_pwm_level(LEDvm, 0);
                sleep_ms(250);
            }
            gera_sequencia();
            level = 1;
            flag_init = 0;
            flag_jogo_perdido = 0;
        }
    }
}