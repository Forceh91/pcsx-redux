#pragma once

#include <EASTL/functional.h>
#include <EASTL/fixed_string.h>
#include <cstdint>
#include <stdint.h>

#include "psyqo/utility-polyfill.h"
#include "third_party/EASTL/include/EASTL/fixed_string.h"

namespace psyqo {
/**
 * @brief An advanced class to access the memory cards.
 *
 * @details This class is meant to be used as a singleton, probably in
 * the `Application` derived class. It does not use the BIOS'
 * Memory Card interface. Instead, it uses the SIO interface directly
 */

 static constexpr uint8_t MAX_MEMORY_CARD_BLOCKS = 15;
 static constexpr uint8_t MC_FILE_NAME_LEN = 21;

class MemoryCard {
  public:
	enum class Card : unsigned {
		MemoryCard1a,
		MemoryCard1b,
		MemoryCard1c,
		MemoryCard1d,
		MemoryCard2a,
		MemoryCard2b,
		MemoryCard2c,
		MemoryCard2d
	};

	enum class CardChecksum : uint8_t {
		Unknown,
		Good, // 0x47
		BadChecksum, // 0x4e
		BadSector // 0xff
	};

	enum class BlockState {
		InUseFirst    = 0x51,
		InUseMiddle   = 0x52,
		InUseLast     = 0x53,
		FreeFormatted = 0xA0,
		FreeDeleted1  = 0xA1,
		FreeDeleted2  = 0xA2,
		FreeDeleted3  = 0xA3,
	};
	
	enum class Region : uint8_t {
		Any,
		Japan, // BI
		Europe, // BE
		America // BA
	};

	union CardData {
		struct {
			uint8_t readDir;
			uint8_t connected;
			MemoryCard::CardChecksum checksum;
			uint8_t sectorData[128]; // each sector is 128 bytes
		};

		uint8_t packed[131];
	};
	
	union DirectoryEntry {
		struct {
			BlockState state;     // 1 byte
			uint32_t fileSize;    // 04h-07h
			uint16_t nextBlock;   // 08h-09h
			char fileName[MC_FILE_NAME_LEN];    // 0Ah-1Eh
			uint8_t unused;       // 1Fh
			uint8_t checksum;     // 7Fh
		};
		uint8_t packed[128];
	};

	void initialize();

	// checks for the prescene of a card, does not verify its not corrupt
	bool detectCard(Card card);

	// full check (takes longer) that will fully verify the header sector of the card
	CardData getCard(Card card);

	// read directory - returns all 15 save slot entries
	// false if there was a read failure, true if success
	bool getDirectory(Card card, DirectoryEntry entries[15]);

	// find a specific save by game ID (e.g. "SCUS-12345"), and optionally region prefix
	// returns slot 0-14, or -1 if not found
	int8_t findSave(const Card card, const eastl::fixed_string<char, MC_FILE_NAME_LEN, false> fileName, Region region = MemoryCard::Region::Any);

	// how many free blocks are available
	int8_t getFreeBlocks(Card card);

	// read save data for a given slot into a buffer
	bool readSave(Card card, int8_t slot, void* buffer, uint16_t size);

	// write save data
	bool writeSave(Card card, int8_t slot, const void* buffer, uint16_t size);

	// format the card (wipes everything). very dangerous!
	bool format(Card card);	

  private:
	uint8_t outputReadCard(unsigned ticks, uint16_t sector = 0);
	CardData sendReadCommand(Card card, uint16_t sector = 0);
	bool waitForAck(); // true if ack received, false if timeout

	const char* m_regionCodes[4] = {"", "BI", "BE", "BA"};
};
} // namespace psyqo
