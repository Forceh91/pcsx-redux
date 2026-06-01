#pragma once

#include <EASTL/functional.h>
#include <stdint.h>

#include "psyqo/utility-polyfill.h"

namespace psyqo {
	union CardData {
		struct {
			uint8_t readDir;
			uint8_t connected;
		};

		uint16_t packed[1];
	};


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

	void initialize();

  CardData getCard(uint8_t port);

  private:
	uint8_t outputDefault(unsigned ticks);
	uint8_t outputDetectCard(unsigned ticks);
	void readCard();
	bool waitForAck(); // true if ack received, false if timeout

	CardData m_cardData[8];
	bool m_connected[8] = {false, false, false, false, false, false, false, false};
	uint8_t m_portToProbe = 0;
	uint8_t m_portsToProbeByVSync = 0;
};
} // namespace psyqo
