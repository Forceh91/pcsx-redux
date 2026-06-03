#include "psyqo/memorycard.hh"
#include "psyqo/hardware/sio0-driver.hh"

#include <cstdint>

#include "psyqo/xprintf.h"
#include "third_party/EASTL/include/EASTL/fixed_string.h"

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
	SIO0.configurePort(static_cast<uint8_t>(card));

	uint8_t *pCardData = reinterpret_cast<uint8_t *>(cardData.packed);
	__builtin_memset(pCardData, 0x00, cardDataWidth);

	if (SIO0.isBusy())
		return false;

	uint8_t dataOut, dataIn;

	// mark as busy since we're about to do memory card stuff
	SIO0.acquire();

	for (unsigned ticks = 0, maxTicks = 4; ticks < maxTicks; ticks++) {
		dataOut = outputReadCard(ticks);
		dataIn = SIO0.transceive(dataOut);

		switch (ticks) {
		case 0: // discard data
			break;

		case 1:
			// FLAG response. will be 0x08 on power up/reinsert
			// Bit3=1 means the directory structure hasn't been read yet
			pCardData[0] = !((dataIn >> 3) & 1);
			break;

		// cards are classed as connected if we get a 5a followed by a 5d
		case 2:
			pCardData[1] = (dataIn == 0x5a); // probably connected
			break;

		case 3:
			pCardData[1] &= (dataIn == 0x5d); // definitely connected
			break;
		}

		// Wait for ACK except on last tick
		if (ticks < (maxTicks - 1)) {
			if (!waitForAck()) {
				// Timeout waiting for ACK
				__builtin_memset(pCardData, 0x00, cardDataWidth);
				break;
			}

			while (SIO::Stat & SIO::Status::STAT_ACK); // Wait for ACK to return to high
		}
	} // tick loop

	// end transmission
    SIO::Ctrl = 0;	

	// finished
	SIO0.release();

	return cardData.connected;
}

psyqo::MemoryCard::CardData psyqo::MemoryCard::getCard(Card card) {
	auto cardData = sendReadCommand(card, 0x000);
	return cardData;
}

bool psyqo::MemoryCard::getDirectory(Card card, DirectoryEntry entries[15]) {
	for (int i = 0; i < MAX_MEMORY_CARD_BLOCKS; i++) {
		auto cardData = sendReadCommand(card, 0x001 + i);
		
		// card isnt connected or we got a bad read, abort
		if (!cardData.connected || cardData.checksum != CardChecksum::Good)
			return false;

		// copy the data over into the entries
		__builtin_memcpy(&entries[i], cardData.sectorData, sizeof(DirectoryEntry));
	}

	return true;
}

int8_t psyqo::MemoryCard::findSave(const Card card, const eastl::fixed_string<char, MC_FILE_NAME_LEN, false> fileName, Region region) {
	int8_t block = -1;
	DirectoryEntry dirEntry;
	uint8_t regionIx = static_cast<uint8_t>(region);

	for (int i = 0; i < MAX_MEMORY_CARD_BLOCKS; i++) {
		auto cardData = sendReadCommand(card, 0x001 + i);
		
		// card isnt connected or we got a bad read, abort
		if (!cardData.connected || cardData.checksum != CardChecksum::Good)
			return block;

		// copy the data over into the entry
		__builtin_memcpy(&dirEntry, cardData.sectorData, sizeof(DirectoryEntry));

		// skip blocks that are empty or not the first block of a save
		if (dirEntry.state != BlockState::InUseFirst)
			continue;

		// see if the region matches
		if (region != Region::Any) {
			if (dirEntry.fileName[0] != m_regionCodes[regionIx][0] || dirEntry.fileName[1] != m_regionCodes[regionIx][1])
				continue;
		}

		// now see if the file name (minus region) matches
		eastl::fixed_string<char, MC_FILE_NAME_LEN, false> tempFileName;
		tempFileName.append(dirEntry.fileName + 2);
		if (tempFileName == fileName) {
			block = i;
			break;
		}
	}

	return block;
}

psyqo::MemoryCard::CardData psyqo::MemoryCard::sendReadCommand(Card card, uint16_t sector) {
	CardData cardData;
	uint8_t dataOut, dataIn;
	
	static constexpr unsigned cardDataWidth = sizeof(CardData);
	SIO0.configurePort(static_cast<uint8_t>(card));

	uint8_t *pCardData = reinterpret_cast<uint8_t *>(cardData.packed);
	__builtin_memset(pCardData, 0x00, cardDataWidth);

	// busy, return blank
	if (SIO0.isBusy())
		return cardData;

	// take full control of SIO0 whilst we do this
	SIO0.acquire();

	uint16_t sectorChecksum = 0;
	for (unsigned ticks = 0, maxTicks = 140; ticks < maxTicks; ticks++) {
		dataOut = outputReadCard(ticks, sector);
		dataIn = SIO0.transceive(dataOut);

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
				sectorChecksum = dataIn;
			break;

			case 9: // confirmed address lsb
				sectorChecksum ^= dataIn;
			break;

			// read the 128 bytes of sector data from 10 - 137
			case 138: // receive checksum
				if (dataIn != sectorChecksum)
					pCardData[2] = static_cast<uint8_t>(MemoryCard::CardChecksum::BadChecksum);
			break;

			case 139: // memory card end byte, should be 0x47 (G)
				if (dataIn == 0x47 && cardData.checksum != CardChecksum::BadChecksum)
					pCardData[2] = static_cast<uint8_t>(MemoryCard::CardChecksum::Good);
				else
					pCardData[2] = static_cast<uint8_t>(MemoryCard::CardChecksum::BadChecksum);
			break;

			// data read
			default:
				sectorChecksum ^= dataIn;

				if (ticks >= 10 && ticks < 138)
					pCardData[3 + (ticks - 10)] = dataIn;
			break;
		}

		// Wait for ACK except on last tick
		if (ticks < (maxTicks - 1)) {
			if (!waitForAck()) {
				// Timeout waiting for ACK
				__builtin_memset(pCardData, 0x00, cardDataWidth);
				break;
			}

			while (SIO::Stat & SIO::Status::STAT_ACK); // Wait for ACK to return to high
		}
	}

	// end transmission
    SIO::Ctrl = 0;

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
