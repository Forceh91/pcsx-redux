#include "psyqo/hardware/cpu.hh"
#include "psyqo/hardware/sio.hh"
#include <cstdint>

using namespace psyqo::Hardware;

class SIO0Driver {
public:
    void initialize() {
        if (m_initialized)
            return;

        SIO::Ctrl = SIO::Control::CTRL_IR;
        SIO::Baud = 0x88; // 250kHz
        SIO::Mode = 0xd;  // MUL1, 8bit, no parity, normal polarity
        SIO::Ctrl = 0;
        m_initialized = true;
    }

    void acquire() { m_busy = true; }
    void release() { m_busy = false; }

    inline uint8_t transceive(uint8_t dataOut) {
        SIO::Ctrl |= SIO::Control::CTRL_ERRRES; // Clear error
        CPU::IReg.clear(CPU::IRQ::Controller);	// Clear IRQ

        SIO::Data = dataOut;

        // Wait for transceive to complete and data to populate FIFO
        while (!(SIO::Stat & SIO::Status::STAT_RXRDY));

        // Pull data from FIFO
        return SIO::Data;        
    }

    void configurePort(uint8_t port) {
        SIO::Ctrl = (port * SIO::Control::CTRL_PORTSEL) | SIO::Control::CTRL_DTR;
        SIO::Baud = 0x88; // 250kHz
        flushRxBuffer();
        SIO::Ctrl |= (SIO::Control::CTRL_TXEN | SIO::Control::CTRL_ACKIRQEN);
        busyLoop(100); // Required delay for pad stability. 100 cycles gives about 23us before the first clock pulse
    }

    const bool isBusy() const { return m_busy; }
private:
	void busyLoop(unsigned delay) {
		unsigned cycles = 0;
		while (++cycles < delay)
			asm("");
	};

    inline void flushRxBuffer() {
        while (SIO::Stat & SIO::Status::STAT_RXRDY) {
            SIO::Data.throwAway(); // throwaway read
        }
    }    
    
    bool m_busy = false;
    bool m_initialized = false;
};

extern SIO0Driver SIO0;
