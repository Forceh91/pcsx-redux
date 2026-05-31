#include "psyqo/memorycard.hh"

#include "common/syscalls//syscalls.h"
#include "psyqo/hardware/cpu.hh"
#include "psyqo/hardware/sio.hh"
#include "psyqo/kernel.hh"
#include "psyqo/utility-polyfill.h"

#include <cstdint>

#include "psyqo/xprintf.h"

using namespace psyqo::Hardware;

static constexpr uint16_t POLLING_INTERVAL = 60;

void psyqo::MemoryCard::initialize() {
	// init cards
	__builtin_memset(m_cardData, 0xff, sizeof(m_cardData));
	m_portsToProbeByVSync = 1;

	SIO::Ctrl = SIO::Control::CTRL_IR;
	SIO::Baud = 0x88; // 250kHz
	SIO::Mode = 0xd;  // MUL1, 8bit, no parity, normal polarity
	SIO::Ctrl = 0;

	m_pollCounter = POLLING_INTERVAL;

	Kernel::Internal::addOnFrame([this]() {
		if (++m_pollCounter < POLLING_INTERVAL)
			return;

		readCard();
		m_pollCounter = 0;

		if (!m_callback)
			return;

		processChanges(Card::MemoryCard1a);
		processChanges(Card::MemoryCard1b);
		processChanges(Card::MemoryCard1c);
		processChanges(Card::MemoryCard1d);
		processChanges(Card::MemoryCard2a);
		processChanges(Card::MemoryCard2b);
		processChanges(Card::MemoryCard2c);
		processChanges(Card::MemoryCard2d);
	});
}

void psyqo::MemoryCard::configurePort(uint8_t port) {
	SIO::Ctrl = (port * SIO::Control::CTRL_PORTSEL) | SIO::Control::CTRL_DTR;
	SIO::Baud = 0x88; // 250kHz
	flushRxBuffer();
	SIO::Ctrl |= (SIO::Control::CTRL_TXEN | SIO::Control::CTRL_ACKIRQEN);
	busyLoop(100); // Required delay for pad stability. 100 cycles gives about 23us before the first clock pulse
}

inline void psyqo::MemoryCard::flushRxBuffer() {
	while (SIO::Stat & SIO::Status::STAT_RXRDY) {
		SIO::Data.throwAway(); // throwaway read
	}
}

uint8_t psyqo::MemoryCard::outputDefault(unsigned ticks) {
	uint8_t dataOut = 0x00;
	switch (ticks) {
	case 0:
		dataOut = 0x81; // memory card address
		break;
	case 1:
		dataOut = 0x52; // Send Read Command (ASCII "R"), Receive FLAG Byte
		break;
	}

	return dataOut;
}

void psyqo::MemoryCard::processChanges(Card card) {
	const unsigned cardIndex = toUnderlying(card);
	bool cardConnected = isCardConnected(card);
	bool wasConnected = m_connected[cardIndex];

	if (wasConnected && !cardConnected)
		m_callback(Event{Event::MemoryCardDisconnected, card});
	else if (!wasConnected && cardConnected)
		m_callback(Event{Event::MemoryCardConnected, card});

	m_connected[cardIndex] = cardConnected;
	// if (!cardConnected) return;
}

inline uint8_t psyqo::MemoryCard::transceive(uint8_t dataOut) {
	SIO::Ctrl |= SIO::Control::CTRL_ERRRES; // Clear error
	CPU::IReg.clear(CPU::IRQ::Controller);	// Clear IRQ

	SIO::Data = dataOut;

	// Wait for transceive to complete and data to populate FIFO
	while (!(SIO::Stat & SIO::Status::STAT_RXRDY));

	// Pull data from FIFO
	return SIO::Data;
}

void psyqo::MemoryCard::readCard() {
	uint8_t dataIn, dataOut;
	uint8_t portDevType[2];

	static constexpr unsigned cardDataWidth = sizeof(CardData);
	const unsigned portsToProbeByVSync = m_portsToProbeByVSync;
	uint8_t port = m_portToProbe;

	for (unsigned i = 0; i < portsToProbeByVSync; i++) {
		printf("[card] probing port=%d\n", port);
		configurePort(port);

		uint8_t *cardData = reinterpret_cast<uint8_t *>(&m_cardData[port * 4].packed);
		__builtin_memset(cardData, 0xff, sizeof(m_cardData[0]));

		for (unsigned ticks = 0, maxTicks = 4; ticks < maxTicks; ticks++) {
			dataOut = outputDefault(ticks);
			dataIn = transceive(dataOut);

			printf("[card] tick=%d, data out=0x%02x data in=0x%02x\n", ticks, dataOut, dataIn);

			// process for single device
			// TODO: add multitap
			switch (ticks) {
			case 0: // discard data
				break;

			case 1:
				// FLAG response. will be 0x08 on power up/reinsert
				// Bit3=1 means the directory structure hasn't been read yet
				cardData[0] = !((dataIn >> 3) & 1);
				printf("[card] flag=0x%02x, readDir=%d\n", dataIn, cardData[0]);
				break;

			// cards are classed as connected if we get a 5a followed by a 5d
			case 2:
				cardData[1] = (dataIn == 0x5a); // probably connected
				break;

			case 3:
				cardData[1] &= (dataIn == 0x5d); // definitely connected
				printf("[card] memory card connected=%d\n", cardData[1]);
				break;

			// default:
			// 	cardData[ticks - 2] = dataIn;
			}

			// Wait for ACK except on last tick
			if (ticks < (maxTicks - 1)) {
				if (!waitForAck()) {
					// Timeout waiting for ACK
					__builtin_memset(cardData, 0x00, cardDataWidth);
					printf("[card] memory card read dir=%d, connected=%d\n", cardData[0], cardData[1]);
					break;
				}

				while (SIO::Stat & SIO::Status::STAT_ACK); // Wait for ACK to return to high
			}
		} // tick loop

		// End transmission
		SIO::Ctrl = 0;
		port ^= 1;
	} // port loop

	m_portToProbe = port;
}

inline bool psyqo::MemoryCard::waitForAck() {
	int cyclesWaited = 0;
	static constexpr int ackTimeout = 0x137; // 137h = ~105us

	while (!(CPU::IReg.isSet(CPU::IRQ::Controller)) && ++cyclesWaited < ackTimeout);

	if (cyclesWaited >= ackTimeout) {
		// Timeout waiting for ACK
		return false;
	}

	return true;
}
