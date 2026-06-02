#pragma once

#include <EASTL/functional.h>
#include <stdint.h>

#include "psyqo/utility-polyfill.h"

namespace psyqo {
/**
 * @brief An advanced class to access the memory cards.
 *
 * @details This class is meant to be used as a singleton, probably in
 * the `Application` derived class. It does not use the BIOS'
 * Memory Card interface. Instead, it uses the SIO interface directly
 */

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

	union CardData {
		struct {
			uint8_t readDir;
			uint8_t connected;
			MemoryCard::CardChecksum checksum;
			uint8_t pad;
		};

		uint16_t packed[2];
	};	

	void initialize();

	// checks for the prescene of a card, does not verify its not corrupt
	bool detectCard(Card card);

	// full check (takes longer) that will fully verify the header sector of the card
	CardData getCard(Card card);

  private:
	uint8_t outputReadCard(unsigned ticks, uint16_t sector = 0);
	void readCard();
	bool waitForAck(); // true if ack received, false if timeout
};
} // namespace psyqo
