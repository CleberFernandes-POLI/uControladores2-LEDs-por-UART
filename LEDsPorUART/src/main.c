#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define EXPECTED_ID     0x01
#define PACKET_SIZE     6
#define BYTE_TIMEOUT_MS 50

/* Códigos de Ação */
#define ACTION_ON       0x01
#define ACTION_OFF      0x02
#define ACTION_TOGGLE   0x03
#define ACAO_PISCA_1S   0x04
#define ACAO_PISCA_2S   0x05

/* Pinos dos LEDs da FRDM-KL25Z */
static const struct gpio_dt_spec led_red   = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_blue  = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

/* Estado global dos LEDs */
static uint8_t rgb_color[3] = {255, 255, 255};
static bool leds_on = false;
static int64_t last_blink_toggle = 0;
static int64_t meio_periodo_pisca_ms = 0;

/* Fila para recepção via interrupção */
K_MSGQ_DEFINE(uart_rx_queue, sizeof(uint8_t), 64, 1);

static void uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	if (!uart_irq_update(dev) || !uart_irq_rx_ready(dev)) {
		return;
	}

	uint8_t byte;
	while (uart_fifo_read(dev, &byte, 1) == 1) {
		k_msgq_put(&uart_rx_queue, &byte, K_NO_WAIT);
	}
}

static void update_leds_hardware(void)
{
	gpio_pin_set_dt(&led_red,   leds_on && (rgb_color[0] > 0));
	gpio_pin_set_dt(&led_green, leds_on && (rgb_color[1] > 0));
	gpio_pin_set_dt(&led_blue,  leds_on && (rgb_color[2] > 0));
}

static uint8_t compute_xor(const uint8_t packet[PACKET_SIZE])
{
	uint8_t result = 0;
	for (int i = 0; i < 5; i++) {
		result ^= packet[i];
	}
	return result;
}

static uint8_t compute_crc8(const uint8_t packet[PACKET_SIZE])
{
	uint8_t crc = 0;
	for (int i = 0; i < 5; i++) {
		crc ^= packet[i];
		for (int bit = 0; bit < 8; bit++) {
			crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
		}
	}
	return crc;
}

static bool validar_pacote(const uint8_t packet[PACKET_SIZE])
{
	if (packet[0] != EXPECTED_ID) {
		return false;
	}
	return (packet[5] == compute_xor(packet)) || (packet[5] == compute_crc8(packet));
}

static void execute_action(const uint8_t packet[PACKET_SIZE])
{
	uint8_t acao = packet[4];

	switch (acao) {
	case ACTION_ON:
		rgb_color[0] = packet[1];
		rgb_color[1] = packet[2];
		rgb_color[2] = packet[3];
		leds_on = true;
		meio_periodo_pisca_ms = 0;
		break;

	case ACTION_OFF:
		leds_on = false;
		meio_periodo_pisca_ms = 0;
		break;

	case ACTION_TOGGLE:
		if (packet[1] != 0 || packet[2] != 0 || packet[3] != 0) {
			rgb_color[0] = packet[1];
			rgb_color[1] = packet[2];
			rgb_color[2] = packet[3];
		}
		leds_on = !leds_on;
		meio_periodo_pisca_ms = 0;
		break;

	case ACAO_PISCA_1S:
	case ACAO_PISCA_2S:
		rgb_color[0] = packet[1];
		rgb_color[1] = packet[2];
		rgb_color[2] = packet[3];
		leds_on = true;
		meio_periodo_pisca_ms = (acao == ACAO_PISCA_1S) ? 500 : 1000;
		last_blink_toggle = k_uptime_get();
		break;

	default:
		printk("Acao invalida: 0x%02X\n", acao);
		return;
	}

	update_leds_hardware();
}

static int char_to_hex(uint8_t c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return -1;
}

static bool parse_hex_text(const uint8_t *text, size_t tamanho, uint8_t packet[PACKET_SIZE])
{
	uint8_t high_nibble = 0;
	bool tem_nibble_alto = false;
	uint8_t index = 0;

	for (size_t i = 0; i < tamanho; i++) {
		uint8_t c = text[i];

		/* Ignora separadores e prefixo 0x */
		if (c == ' ' || c == ',' || c == '-') continue;
		if (c == '0' && i + 1 < tamanho && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
			i++;
			continue;
		}

		int value = char_to_hex(c);
		if (value < 0) return false;

		if (!tem_nibble_alto) {
			high_nibble = (uint8_t)value;
			tem_nibble_alto = true;
		} else {
			if (index >= PACKET_SIZE) return false;
			packet[index++] = (uint8_t)((high_nibble << 4) | value);
			tem_nibble_alto = false;
		}
	}

	return (index == PACKET_SIZE) && !tem_nibble_alto;
}

static void processar_entrada_texto(const uint8_t *buffer, size_t tamanho)
{
	uint8_t packet[PACKET_SIZE];

	if (!parse_hex_text(buffer, tamanho, packet)) {
		printk("Erro: formato HEX invalido.\n");
		return;
	}

	if (!validar_pacote(packet)) {
		printk("Erro: ID ou Checksum incorreto (ID=0x%02X, CHK=0x%02X).\n", packet[0], packet[5]);
		return;
	}

	printk("Sucesso -> R:%u G:%u B:%u | Acao:0x%02X\n", packet[1], packet[2], packet[3], packet[4]);
	execute_action(packet);
}

static void initial_led_test(void)
{
	const struct gpio_dt_spec *leds[] = {&led_red, &led_green, &led_blue};
	for (int i = 0; i < 3; i++) {
		gpio_pin_set_dt(leds[i], 1);
		k_sleep(K_MSEC(150));
		gpio_pin_set_dt(leds[i], 0);
	}
}

int main(void)
{
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (!device_is_ready(uart) ||
	    !device_is_ready(led_red.port) ||
	    !device_is_ready(led_green.port) ||
	    !device_is_ready(led_blue.port)) {
		return 0;
	}

	gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);

	initial_led_test();

	uart_irq_callback_user_data_set(uart, uart_isr, NULL);
	uart_irq_rx_enable(uart);

	printk("Receptor UART pronto. Envie o pacote em HEX (ex: 01FF000001FF)\n");

	uint8_t text_buffer[32];
	size_t text_index = 0;
	int64_t tempo_ultimo_byte = 0;

	while (1) {
		uint8_t byte;
		int64_t now = k_uptime_get();

		/* Lê bytes da fila da interrupção */
		while (k_msgq_get(&uart_rx_queue, &byte, K_NO_WAIT) == 0) {
			now = k_uptime_get();

			if (byte == '\r' || byte == '\n') {
				if (text_index > 0) {
					processar_entrada_texto(text_buffer, text_index);
					text_index = 0;
				}
				continue;
			}

			if (text_index < sizeof(text_buffer)) {
				text_buffer[text_index++] = byte;
				tempo_ultimo_byte = now;
			} else {
				text_index = 0; /* Estouro de buffer: descarta */
			}
		}

		/* Timeout: processa o que chegou caso não tenha sido enviado Enter (\n) */
		if (text_index > 0 && (now - tempo_ultimo_byte) > BYTE_TIMEOUT_MS) {
			processar_entrada_texto(text_buffer, text_index);
			text_index = 0;
		}

		/* Controle do pisca-pisca não bloqueante */
		if (meio_periodo_pisca_ms > 0 && (now - last_blink_toggle) >= meio_periodo_pisca_ms) {
			leds_on = !leds_on;
			last_blink_toggle = now;
			update_leds_hardware();
		}

		k_sleep(K_MSEC(1));
	}
}