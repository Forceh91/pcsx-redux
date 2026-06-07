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

uint8_t psyqo::MemoryCard::outputWriteCard(unsigned ticks, uint16_t sector, void* buffer) {
	uint8_t dataOut = 0x00;
	switch (ticks) {
		case 0:
			dataOut = 0x81; // memory card address
			break;

		case 1:
			dataOut = 0x57; // Send Write Command (ASCII "W"), Receive FLAG Byte
			break;

		case 4:
			dataOut = (sector >> 8) & 0xFF; // sector MSB
			m_writeChecksum ^= dataOut;
		break;

		case 5:
			dataOut = sector & 0xFF; // sector LSB
			m_writeChecksum ^= dataOut;
		break;

		case 134: // send checksum
			dataOut = m_writeChecksum;
		break;

		default:
			if (ticks >= 6 && ticks <= 133) {
				// send buffer byte-by-byte
				dataOut = static_cast<uint8_t*>(buffer)[ticks - 6];
				m_writeChecksum ^= dataOut;
			}
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
		dataOut = outputReadCard(ticks, 0x00);
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
	for (int i = 0; i < MC_MAX_BLOCKS; i++) {
		auto cardData = sendReadCommand(card, 0x001 + i);
		
		// card isnt connected or we got a bad read, abort
		if (!cardData.connected || cardData.checksum != CardChecksum::Good)
			return false;

		// copy the data over into the entries
		__builtin_memcpy(&entries[i], cardData.sectorData, sizeof(DirectoryEntry));
	}

	return true;
}

psyqo::MemoryCard::SaveBlock psyqo::MemoryCard::findSave(const Card card, const eastl::fixed_string<char, MC_FILE_NAME_LEN, false> fileName, Region region) {
	psyqo::MemoryCard::SaveBlock save;
	DirectoryEntry dirEntry;
	uint8_t regionIx = static_cast<uint8_t>(region);

	for (int8_t i = 0; i < MC_MAX_BLOCKS; i++) {
		auto cardData = sendReadCommand(card, 0x001 + i);
		
		// card isnt connected or we got a bad read, abort
		if (!cardData.connected || cardData.checksum != CardChecksum::Good)
			return save;

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
			save = { static_cast<int8_t>(i + 1), static_cast<uint16_t>(dirEntry.fileSize), dirEntry.state};
			break;
		}
	}

	return save;
}

uint8_t psyqo::MemoryCard::getFreeBlocks(Card card) {
	DirectoryEntry dirEntry;
	uint8_t freeBlocks = 0;

	for (int i = 0; i < MC_MAX_BLOCKS; i++) {
		auto cardData = sendReadCommand(card, 0x001 + i);
		
		// card isnt connected or we got a bad read, abort
		if (!cardData.connected || cardData.checksum != CardChecksum::Good)
			return freeBlocks;

		// copy the data over into the entry
		__builtin_memcpy(&dirEntry, cardData.sectorData, sizeof(DirectoryEntry));

		if (dirEntry.state >= BlockState::FreeFormatted && dirEntry.state <= BlockState::FreeDeleted3)
			freeBlocks++;
	}

	return freeBlocks;
}

int8_t psyqo::MemoryCard::getFirstFreeBlock(Card card) {
	DirectoryEntry dirEntry;
	int8_t firstFreeBlock = -1;

	for (int i = 0; i < MC_MAX_BLOCKS; i++) {
		auto cardData = sendReadCommand(card, 0x001 + i);
		
		// card isnt connected or we got a bad read, abort
		if (!cardData.connected || cardData.checksum != CardChecksum::Good)
			return firstFreeBlock;

		// copy the data over into the entry
		__builtin_memcpy(&dirEntry, cardData.sectorData, sizeof(DirectoryEntry));

		if (dirEntry.state >= BlockState::FreeFormatted && dirEntry.state <= BlockState::FreeDeleted3)
			return i + 1;
	}

	return firstFreeBlock;
}

uint16_t psyqo::MemoryCard::readSaveSlot(Card card, uint8_t slot, BlockState blockState, void* buffer) {
	uint16_t blockSector = slot * 64, bytesRead = 0;
	uint8_t startingSector = 0;

	// read the title sector
	if (blockState == BlockState::InUseFirst) {
		// get back sector data
		auto cardData = sendReadCommand(card, blockSector);
		if (!cardData.connected || cardData.checksum != CardChecksum::Good)
			return 0;

		startingSector++;

		// figure out how many icons, with a sanity check
		auto iconFrames = cardData.sectorData[2] - 0x010;
		if (iconFrames < 1 || iconFrames > 3)
			return 0;

		startingSector += iconFrames; // 0x011 = 1, 0x012 = 2, 0x013 = 3
	}

	// skip over icon frames and start reading the actual data
	for (int sector = startingSector; sector < 64; sector++) {
		auto cardData = sendReadCommand(card, blockSector + sector);
		if (!cardData.connected || cardData.checksum != CardChecksum::Good)
			return 0;

		// copy into the buffer, and advance the pointer forward for the next sector
		__builtin_memcpy(buffer, cardData.sectorData, 128);
		buffer = static_cast<uint8_t*>(buffer) + 128;
		bytesRead += 128;
	}

	return bytesRead;
}

bool psyqo::MemoryCard::readSave(Card card, uint8_t slot, void* buffer) {
	int16_t nextBlock = slot;

	// read the first block
	while (nextBlock != 0) {
		// get the block data by reading the directory at 0x001 + slot 0 indexed.
		auto cardData = sendReadCommand(card, 0x001 + (nextBlock - 1));
		
		auto blockState = static_cast<BlockState>(cardData.sectorData[0]);
		
		uint32_t fileSize;
		__builtin_memcpy(&fileSize, &cardData.sectorData[4], sizeof(uint32_t));

		// read the save data from this slot. stop if its a bad read
		auto resp = readSaveSlot(card, nextBlock, blockState, buffer);
		if (!resp)
			return resp;

		// whats the nextblock if any?
		__builtin_memcpy(&nextBlock, &cardData.sectorData[8], sizeof(int16_t));
		nextBlock += 1;			

		// no more left to read
		if (nextBlock == 0)
			return true;

		// increase buffer for next block
		buffer = static_cast<uint8_t*>(buffer) + resp;
	}

	return true;
}

psyqo::MemoryCard::WriteResult psyqo::MemoryCard::writeSave(Card card, const char* fileName, void* buffer, uint16_t size, SaveTitle titleInfo, SaveIcon iconInfo) {
	// validate we have enough free blocks
	auto requiredBlocks = (size + MC_BLOCK_SIZE) / MC_BLOCK_SIZE;
	uint8_t freeSlotCount = getFreeBlocks(card);
	if (freeSlotCount < requiredBlocks)
		return WriteResult::CardFull;

	// find a free slot
	int8_t firstFreeBlock = getFirstFreeBlock(card);
	if (firstFreeBlock == -1)
		return WriteResult::CardFull;

	// validate the icon count
	if (iconInfo.count < IconCount::Static || iconInfo.count > IconCount::Animated3)
		return WriteResult::InvalidIconCount;

	// which sector are we writing?
	uint16_t blockSector = firstFreeBlock * 64;
	uint8_t blockSectorOffset = 0;
	uint16_t bytesWritten = 0;

	// prepare the Title sector for writing
	SaveBlockTitleSector titleSector;
	__builtin_memset(&titleSector, 0x00, sizeof(titleSector));
	__builtin_memcpy(&titleSector.id, "SC", 2);
	titleSector.count = iconInfo.count;
	titleSector.blockNumber = 1;

	// now Shift-JS support yet.. just ascii
	__builtin_memcpy(&titleSector.title, titleInfo.title.c_str(), titleInfo.title.size());
	__builtin_memcpy(&titleSector.iconColorPallete, iconInfo.clut, 32);

	// send the title block
	auto cardData = sendWriteCommand(card, blockSector, titleSector.packed);
	if (!cardData.connected)
		return WriteResult::NoCard;

	if (cardData.checksum != CardChecksum::Good)
		return handleWriteChecksum(cardData.checksum);		

	// send the icon block/s
	auto iconFrameCount = static_cast<uint8_t>(iconInfo.count) - 0x10;
	for (auto i = 0; i < iconFrameCount; i++) {
		auto buffer = static_cast<uint8_t*>(iconInfo.bitmap) + (i * 128);
		cardData = sendWriteCommand(card, blockSector + ++blockSectorOffset, buffer);
		if (!cardData.connected)
			return WriteResult::NoCard;

		if (cardData.checksum != CardChecksum::Good)
			return handleWriteChecksum(cardData.checksum);
	}

	// send the data block/s
	for (int i = blockSectorOffset + 1, bufferIx = 0; i < 64; i++) {
		auto dataBuffer = static_cast<uint8_t*>(buffer) + (bufferIx * 128);
		cardData = sendWriteCommand(card, blockSector + i, dataBuffer);
		if (!cardData.connected)
			return WriteResult::NoCard;

		if (cardData.checksum != CardChecksum::Good)
			return handleWriteChecksum(cardData.checksum);

		bytesWritten += 128;
		bufferIx++;

		if (bytesWritten >= size)
			break;
	}

	// update the ToC if good.
	auto region = static_cast<uint8_t>(titleInfo.region);
	eastl::fixed_string<char, MC_FILE_NAME_LEN, false> regionFileName = m_regionCodes[region];
	regionFileName.append(fileName);

	DirectoryEntry dirEntry;
	__builtin_memset(&dirEntry, 0x00, 128);
	
	dirEntry.state = BlockState::InUseFirst;
	dirEntry.fileSize = static_cast<uint32_t>(requiredBlocks * MC_BLOCK_SIZE),
	dirEntry.nextBlock = 0xffff;
	__builtin_memcpy(dirEntry.fileName, regionFileName.c_str(), MC_FILE_NAME_LEN);
	__builtin_memset(dirEntry.garbage, 0x00, sizeof(dirEntry.garbage));
	dirEntry.checksum = generateChecksum(dirEntry.packed);

	cardData = sendWriteCommand(card, 0x001 + (firstFreeBlock - 1), dirEntry.packed);
	
	if (!cardData.connected)
		return WriteResult::NoCard;
	if (cardData.checksum != CardChecksum::Good)
		return handleWriteChecksum(cardData.checksum);

	return WriteResult::Good;
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

psyqo::MemoryCard::CardData psyqo::MemoryCard::sendWriteCommand(Card card, uint16_t sector, void* buffer) {
	CardData cardData;
	uint8_t dataOut, dataIn;

	static constexpr unsigned cardDataWidth = sizeof(cardData);
	SIO0.configurePort(static_cast<uint8_t>(card));

	uint8_t* pCardData = reinterpret_cast<uint8_t*>(cardData.packed);
	__builtin_memset(pCardData, 0x00, cardDataWidth);

	// somethings already going on, we shouldn't interrupt
	if (SIO0.isBusy())
		return cardData;

	// we need full control over SIO0 when we do this
	SIO0.acquire();

	m_writeChecksum = 0;
	for (unsigned ticks = 0, maxTicks = 138; ticks < maxTicks; ticks++) {
		dataOut = outputWriteCard(ticks, sector, buffer);
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
			
			case 134: // CHK (again 0x0 or sector)
			break;

			case 135: // ACK1
			break;

			case 136: // ACK2
			break;

			case 137: // End Byte (47h=Good, 4Eh=BadChecksum, FFh=BadSector)
				if (dataIn == 0x47)
					cardData.checksum = CardChecksum::Good;
				else if (dataIn == 0xff)
					cardData.checksum = CardChecksum::BadSector;
				else
					cardData.checksum = CardChecksum::BadChecksum;
			break;

			default:
				// response to DATA bytes. likely the sector number?
				if (ticks >= 6 && ticks <= 133) {
				}	
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
	static constexpr int ackTimeout = 0x10000; // 137h = ~105us

	while (!(CPU::IReg.isSet(CPU::IRQ::Controller)) && ++cyclesWaited < ackTimeout);

	if (cyclesWaited >= ackTimeout) {
		// Timeout waiting for ACK
		return false;
	}

	return true;
}

uint8_t psyqo::MemoryCard::generateChecksum(void* buffer) {
	uint8_t checksum = 0;
	auto bytes = static_cast<uint8_t*>(buffer);
	for (int i = 0; i < 127; i++)
		checksum ^= bytes[i];

	return checksum;
}

psyqo::MemoryCard::WriteResult psyqo::MemoryCard::handleWriteChecksum(psyqo::MemoryCard::CardChecksum checksum) {
	if (checksum == CardChecksum::BadChecksum)
		return WriteResult::BadChecksum;

	if (checksum == CardChecksum::BadSector)
		return WriteResult::BadSector;
}
