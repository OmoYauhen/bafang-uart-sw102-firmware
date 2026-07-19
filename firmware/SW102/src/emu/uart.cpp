/*
 * Bafang LCD SW102 Bluetooth firmware
 *
 * Copyright (C) lowPerformer, 2019.
 *
 * Released under the GPL License, Version 3
 */
extern "C" {
#include <string.h>
#include "common.h"
#include "uart.h"
#include "utils.h"
#include "assert.h"

extern uint16_t emu_voltage;
}

#include <QSerialPort>
#include <QSerialPortInfo>
#include <QDebug>

static QSerialPort port;

// Bafang display UART: 1200 baud, 8-N-1.
#define BAFANG_BAUD 1200

void uart_init()
{
	const char *override = getenv("SW102_UART_PORT");
	if (override) {
		port.setPortName(override);
		port.setBaudRate(BAFANG_BAUD);
		if(port.open(QIODevice::ReadWrite)) {
			qDebug() << "Opened (override)" << override;
			emu_voltage = 480;
			return;
		}
		qDebug() << "Failed to open SW102_UART_PORT" << override;
		return;
	}

	for(auto & pi: QSerialPortInfo::availablePorts()) {
		if(pi.portName().startsWith("ttyUSB")) {
			port.setPort(pi);
			port.setBaudRate(BAFANG_BAUD);
			if(port.open(QIODevice::ReadWrite)) {
				qDebug() << "Opened" << pi.portName();
				emu_voltage = 480;
				return;
			}
		}
	}

	qDebug() << "No USB UART connected";
}
static uint8_t ui8_rx[UART_NUMBER_DATA_BYTES_TO_RECEIVE];
static uint8_t ui8_rx_cnt = 0;
static uint8_t ui8_tx_buffer[UART_NUMBER_DATA_BYTES_TO_SEND];
static uint8_t ui8_expected_rx_len = 0;
volatile uint8_t ui8_received_package_flag = 0;

uint8_t* uart_get_tx_buffer(void)
{
	return ui8_tx_buffer;
}

extern "C" void uart_prime_rx(uint8_t expected_len)
{
	ui8_expected_rx_len = expected_len;
	ui8_rx_cnt = 0;
	ui8_received_package_flag = 0;
}

/**
 * @brief Send TX buffer over UART.
 */
void uart_send_tx_buffer(uint8_t *tx_buffer, uint8_t ui8_len)
{
	port.write((char*)tx_buffer, ui8_len);
}

// Bafang framing: no start byte, no length byte, no CRC. Caller uses
// uart_prime_rx() to declare how many bytes to expect for the next reply.
static const uint8_t *uart_process_rx(uint8_t ui8_byte_received)
{
	if (ui8_expected_rx_len == 0 || ui8_received_package_flag)
		return NULL;

	if (ui8_rx_cnt < ui8_expected_rx_len) {
		ui8_rx[ui8_rx_cnt++] = ui8_byte_received;
		if (ui8_rx_cnt == ui8_expected_rx_len) {
			ui8_received_package_flag = 1;
			return ui8_rx;
		}
	}
	return NULL;
}

#include <unistd.h>
const uint8_t* uart_get_rx_buffer_rdy(void)
{
	char c;
	if(!port.isOpen())
		return NULL;

	port.waitForReadyRead(0);
	while(port.getChar(&c)) {
		const uint8_t *r = uart_process_rx((uint8_t)c);
		if(r)
			return r;
	}
	return NULL;
}


