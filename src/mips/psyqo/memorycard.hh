#pragma once

#include <EASTL/functional.h>
#include <EASTL/fixed_string.h>
#include <EASTL/span.h>
#include <EASTL/fixed_vector.h>
#include <cstdint>

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

 static constexpr uint8_t MC_MAX_BLOCKS = 15;
 static constexpr uint8_t MC_FILE_NAME_LEN = 21;
 static constexpr uint16_t MC_BLOCK_SIZE = 8192;

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
		// ....
		FreeDeletedLast = 0xAF
	};
	
	enum class Region : uint8_t {
		Any,
		Japan, // BI
		Europe, // BE
		America // BA
	};

	enum class WriteResult : uint8_t {
		Good,
		BadChecksum,
		BadSector,
		CardFull,
		NoCard,
		InvalidSlot,
		InvalidIconCount,
		TitleTooLong,
		Corrupted,
	};

	enum class IconCount : uint8_t {
		Static = 0x11,      // 0x11 - single frame, shown forever
		Animated2 = 0x12,   // 0x12 - 2 frames, changes every 16 PAL frames
		Animated3 = 0x13,   // 0x13 - 3 frames, changes every 11 PAL frames
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
			BlockState state;     				// 00h-03h
			uint32_t fileSize;    				// 04h-07h
			uint16_t nextBlock;   				// 08h-09h
			char fileName[MC_FILE_NAME_LEN];    // 0Ah-1Eh
			uint8_t unused;       				// 1Fh
			uint8_t garbage[95];				// 0x20-0x7E
			uint8_t checksum;     				// 7Fh
		};
		uint8_t packed[128];
	};

	union SaveBlock {
		struct {
			int8_t slot = -1; // 1-indexed, -1 if not found
			uint16_t size = 0; // filesize from dir entry
			BlockState state = BlockState::FreeFormatted;
		};

		uint8_t packed[7];
	};

	union SaveBlockTitleSector {
		struct {
			char id[2];
			IconCount count;
			uint8_t blockNumber; // ???
			char title[64];
			uint8_t reserved1[12];
			uint8_t reserved2[16]; // pocketstation
			uint8_t iconColorPallete[32]; // each entry is a 16bit CLUT
		};

		uint8_t packed[128];
	};

	struct SaveTitle {
		const eastl::fixed_string<char, 32> title;
		Region region = Region::Any;
	};

	struct SaveIcon {
		void* clut;        // 32 bytes - 16 color palette entries (16bit each)
		void* bitmap;      // 128 bytes per frame
		IconCount count = IconCount::Static;
	};

	void initialize();

	// checks for the prescene of a card, does not verify its not corrupt
	bool detectCard(Card card);

	// full check (takes longer) that will fully verify the header sector of the card
	CardData getCard(Card card);

	// read directory - returns all 15 save slot entries
	// false if there was a read failure, true if success
	bool getDirectory(Card card, DirectoryEntry entries[15]);

	// find a specific save by filename (e.g. "SLUS-00855SYSTEMDT"), and optionally filter by region
	// returns SaveBlock with slot (1-15, or -1 if not found), file size, and block state
	SaveBlock findSave(const Card card, const eastl::fixed_string<char, MC_FILE_NAME_LEN, false> fileName, Region region = MemoryCard::Region::Any);

	// how many free blocks are available
	uint8_t getFreeBlockCount(Card card);
	
	// what free blocks are available
	eastl::span<int8_t> getFreeBlocks(Card card);

	// read save data for a given slot into a buffer
	// slot 1 - 15 are valid, 0 is reserved
	// the size of your buffer should be the number of save slots * 8192.
	bool readSave(Card card, uint8_t slot, void* buffer);

	// write save data
	WriteResult writeSave(Card card, const char* fileName, void* buffer, uint16_t size, SaveTitle titleInfo, SaveIcon iconInfo);

	// format the card (wipes everything). very dangerous!
	bool format(Card card);	

  private:
  	uint16_t readSaveSlot(Card card, uint8_t slot, BlockState blockState, void* buffer);
	uint8_t outputReadCard(unsigned ticks, uint16_t sector);
	uint8_t outputWriteCard(unsigned ticks, uint16_t sector, void* buffer);
	CardData sendReadCommand(Card card, uint16_t sector = 0);
	CardData sendWriteCommand(Card card, uint16_t sector, void* buffer);
	bool waitForAck(); // true if ack received, false if timeout

	int8_t getFirstFreeBlock(Card card);
	uint8_t generateChecksum(void* buffer);
	WriteResult handleWriteChecksum(psyqo::MemoryCard::CardChecksum checksum);

	const char* m_regionCodes[4] = {"", "BI", "BE", "BA"};

	uint16_t m_writeChecksum = 0;
};
} // namespace psyqo
