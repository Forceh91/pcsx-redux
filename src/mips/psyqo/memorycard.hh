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

	struct Event {
		enum { MemoryCardConnected, MemoryCardDisconnected } type;
		Card memoryCard;
	};

	void initialize();

    /**
     * @brief Returns the state of a card.
     *
     * @details Returns the state of a card. The state is a boolean value
     * that is `true` if the card is connected, and `false` otherwise.
     *
     * @param card The card to query.
     * @return A boolean value indicating whether the card is connected.
     */
    bool isCardConnected(Card card) const { return m_cardData[toUnderlying(card)].connected; }

    /**
     * @brief Returns whether the card has had its directory structure read
     *
     * @details Returns the state of a card. The state is a boolean value
     * that is `true` if the card has had its directory structure read, and `false` otherwise.
	 * This is useful to know if a new card has been inserted since the last time we checked this slot.
     *
     * @param card The card to query.
     * @return A boolean value indicating whether the card is connected.
     */	
	bool hasCardReadDirStructure(Card card) const { return m_cardData[toUnderlying(card)].readDir; }

  private:
	union CardData {
		struct {
			uint8_t readDir;
			uint8_t connected;
		};

		uint16_t packed[1];
	};

	void busyLoop(unsigned delay) {
		unsigned cycles = 0;
		while (++cycles < delay)
			asm("");
	};

	void configurePort(uint8_t port);
    void flushRxBuffer();
    uint8_t outputDefault(unsigned ticks);
    void processChanges(Card card);
    void readCard();
    uint8_t transceive(uint8_t dataOut);
    bool waitForAck();  // true if ack received, false if timeout

	CardData m_cardData[8];
    eastl::function<void(Event)> m_callback;
	bool m_connected[8] = {false, false, false, false, false, false, false, false};
    uint8_t m_portToProbe = 0;
    uint8_t m_portsToProbeByVSync = 0;
	uint16_t m_pollCounter = 0;
};
} // namespace psyqo
