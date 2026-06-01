#include "psyqo/memorycard.hh"

#include "psyqo/hardware/sio0-driver.hh"

#include <cstdint>

#include "psyqo/xprintf.h"

using namespace psyqo::Hardware;

void psyqo::MemoryCard::initialize() {
	// init cards
	__builtin_memset(m_cardData, 0xff, sizeof(m_cardData));
	m_portsToProbeByVSync = 1;

	SIO0.initialize();
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

uint8_t psyqo::MemoryCard::outputDetectCard(unsigned ticks) {
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

psyqo::CardData psyqo::MemoryCard::getCard(uint8_t port) {
	CardData cardData;
	uint8_t dataOut, dataIn;

	// mark as busy since we're about to do memory card stuff
	SIO0.acquire();

	static constexpr unsigned cardDataWidth = sizeof(CardData);
	printf("[card] probing port %d\n", port);
	SIO0.configurePort(port);

	uint8_t *pCardData = reinterpret_cast<uint8_t *>(cardData.packed);
	__builtin_memset(pCardData, 0xff, cardDataWidth);

	for (unsigned ticks = 0, maxTicks = 4; ticks < maxTicks; ticks++) {
		dataOut = outputDetectCard(ticks);
		dataIn = SIO0.transceive(dataOut);

		printf("[card] tick=%d, data out=0x%02x data in=0x%02x\n", ticks, dataOut, dataIn);

		switch (ticks) {
		case 0: // discard data
			break;

		case 1:
			// FLAG response. will be 0x08 on power up/reinsert
			// Bit3=1 means the directory structure hasn't been read yet
			pCardData[0] = !((dataIn >> 3) & 1);
			printf("[card] flag=0x%02x, readDir=%d\n", dataIn, pCardData[0]);
			break;

		// cards are classed as connected if we get a 5a followed by a 5d
		case 2:
			pCardData[1] = (dataIn == 0x5a); // probably connected
			break;

		case 3:
			pCardData[1] &= (dataIn == 0x5d); // definitely connected
			printf("[card] memory card connected=%d\n", pCardData[1]);
			break;

			// default:
			// 	cardData[ticks - 2] = dataIn;
		}

		// Wait for ACK except on last tick
		if (ticks < (maxTicks - 1)) {
			if (!waitForAck()) {
				// Timeout waiting for ACK
				__builtin_memset(pCardData, 0x00, cardDataWidth);
				printf("[card] memory card read dir=%d, connected=%d\n", pCardData[0], pCardData[1]);
				break;
			}

			while (SIO::Stat & SIO::Status::STAT_ACK); // Wait for ACK to return to high
		}
	} // tick loop

	// finished
	SIO0.release();

	return cardData;
}

void psyqo::MemoryCard::readCard() {
	uint8_t dataIn, dataOut;
	uint8_t portDevType[2];

	static constexpr unsigned cardDataWidth = sizeof(CardData);
	const unsigned portsToProbeByVSync = m_portsToProbeByVSync;
	uint8_t port = m_portToProbe;

	for (unsigned i = 0; i < portsToProbeByVSync; i++) {
		printf("[card] probing port=%d\n", port);
		SIO0.configurePort(port);

		uint8_t *cardData = reinterpret_cast<uint8_t *>(&m_cardData[port * 4].packed);
		__builtin_memset(cardData, 0xff, sizeof(m_cardData[0]));

		for (unsigned ticks = 0, maxTicks = 4; ticks < maxTicks; ticks++) {
			dataOut = outputDefault(ticks);
			dataIn = SIO0.transceive(dataOut);

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

				while (SIO::Stat & SIO::Status::STAT_ACK)
					; // Wait for ACK to return to high
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

	while (!(CPU::IReg.isSet(CPU::IRQ::Controller)) && ++cyclesWaited < ackTimeout)
		;

	if (cyclesWaited >= ackTimeout) {
		// Timeout waiting for ACK
		return false;
	}

	return true;
}
