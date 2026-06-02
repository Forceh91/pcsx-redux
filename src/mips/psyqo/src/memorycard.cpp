#include "psyqo/memorycard.hh"
#include "psyqo/hardware/sio0-driver.hh"

#include <cstdint>

#include "psyqo/xprintf.h"

using namespace psyqo::Hardware;

void psyqo::MemoryCard::initialize() {
	SIO0.initialize();
}

uint8_t psyqo::MemoryCard::outputReadCard(unsigned ticks, uint16_t sector) {
	uint8_t dataOut = 0x00;
	switch (ticks) {
		case 0:
			dataOut = 0x81; // memory card address
			break;
		case 1:
			dataOut = 0x52; // Send Read Command (ASCII "R"), Receive FLAG Byte
			break;

		case 4:
			dataOut = (sector >> 8) & 0xFF; // sector MSB
		break;

		case 5:
			dataOut = sector & 0xFF; // sector LSB
		break;
	}

	return dataOut;
}

bool psyqo::MemoryCard::detectCard(Card card) {
	CardData cardData;
	
	static constexpr unsigned cardDataWidth = sizeof(CardData);
	printf("[card] probing port %d\n", card);
	SIO0.configurePort(static_cast<uint8_t>(card));

	uint8_t *pCardData = reinterpret_cast<uint8_t *>(cardData.packed);
	__builtin_memset(pCardData, 0xff, cardDataWidth);

	if (SIO0.isBusy())
		return false;

	uint8_t dataOut, dataIn;

	// mark as busy since we're about to do memory card stuff
	SIO0.acquire();

	for (unsigned ticks = 0, maxTicks = 4; ticks < maxTicks; ticks++) {
		dataOut = outputReadCard(ticks);
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

	return cardData.connected;
}

psyqo::CardData psyqo::MemoryCard::getCard(Card card) {
	CardData cardData;
	uint8_t dataOut, dataIn;
	
	static constexpr unsigned cardDataWidth = sizeof(CardData);
	printf("[card] probing port %d\n", card);
	SIO0.configurePort(static_cast<uint8_t>(card));

	uint8_t *pCardData = reinterpret_cast<uint8_t *>(cardData.packed);
	__builtin_memset(pCardData, 0xff, cardDataWidth);

	// busy, return blank
	if (SIO0.isBusy())
		return cardData;

	// take full control of SIO0 whilst we do this
	SIO0.acquire();

	uint16_t sector = 0x0000;
	for (unsigned ticks = 0, maxTicks = 13; ticks < maxTicks; ticks++) {
		dataOut = outputReadCard(ticks, sector);
		dataIn = SIO0.transceive(dataOut);

		printf("[card] tick=%d, data out=0x%02x data in=0x%02x\n", ticks, dataOut, dataIn);

		switch (ticks) {
			case 0: // discard
			break;

			case 1: // FLAG
				pCardData[0] = !((dataIn >> 3) & 1);
			break;

			case 2: // id1
				pCardData[1] = (dataIn == 0x5a); // probably connected
			break;

			case 3: // id2
				pCardData[1] &= (dataIn == 0x5d); // definitely connected
			break;

			case 4: // MSB (sector number?)
			break;

			case 5: // LSB
			break;

			case 6: // command ack1
			break;

			case 7: // command ack2
			break;

			case 8: // confirmed address msb
			break;

			case 9: // confirmed address lsb
			break;

			// sector is 0x0000, so we start receiving the header frame
			case 10: // should get back "M"
			break;

			case 11: // should get back "C"
			break;
		}

		// Wait for ACK except on last tick
		if (ticks < (maxTicks - 1)) {
			if (!waitForAck()) {
				printf("ack!\n");
				// Timeout waiting for ACK
				__builtin_memset(pCardData, 0x00, cardDataWidth);
				break;
			}

			while (SIO::Stat & SIO::Status::STAT_ACK); // Wait for ACK to return to high
		}		
	}

	printf("memory card header passed.\n");

	// finished reading, controller can have input back
	SIO0.release();

	return cardData;
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
