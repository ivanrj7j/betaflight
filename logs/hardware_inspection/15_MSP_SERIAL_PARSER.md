# Hardware Inspection Log: `src/main/msp/msp_serial.c`

## 1. File Purpose & Role in Architecture
* **File Path**: `src/main/msp/msp_serial.c`
* **Subsystem**: Serial MultiWii Serial Protocol (MSP) Session Manager, Non-Blocking Packet State Machine, and Configurator Handshake Engine.
* **Key Components**:
  * `mspSerialAllocatePorts()`: Connects serial ports designated for `FUNCTION_MSP` to the MSP protocol engine.
  * `mspSerialProcessReceivedPacketData()`: Zero-allocation byte-by-byte finite state machine supporting MSP v1 (XOR) and MSP v2 (CRC8-DVB).
  * `mspSerialProcess()`: Periodic task reading incoming serial ring buffers, executing commands, and flushing outgoing responses.

---

## 2. Hardware Challenges STM32 Developers Faced & How They Overcame Them

### Challenge 1: Handling Stream Corruption & Non-MSP Traffic (Garbage Rejection)
* **The Problem**: When connecting via USB or Bluetooth, line noise, ESC chatter, or host auto-baud probing can send random bytes before the first valid MSP packet. If the parser enters an invalid state or assumes fixed byte offsets, it hangs or rejects all subsequent valid packets.
* **How It Was Overcome**:
  The parser implements a resilient resynchronization state machine:
  * Any unexpected character encountered before completing a frame immediately forces `mspPort->packetState = MSP_IDLE`.
  * The parser only transitions to `MSP_HEADER_M` or `MSP_HEADER_X` when it encounters `'$'`.
  * Checksums (XOR for v1, CRC8 for v2) ensure corrupted frames are discarded silently without executing invalid commands.

---

### Challenge 2: Heap Exhaustion and Fragmentation Prevention
* **The Problem**: On embedded targets with limited RAM (e.g. 128KB on STM32F411 or 248KB on ARIES v3.0), dynamically allocating memory (`malloc`) for every incoming MSP request causes heap fragmentation and memory leaks over long Configurator sessions.
* **How It Was Overcome**:
  The MSP engine is completely **heapless (static memory allocation)**:
  * `mspPorts[MSP_PORT_COUNT]` is statically allocated in `.bss`.
  * Input and output buffers (`inBuf`, `outBuf`) use fixed compile-time limits (`MSP_PORT_INBUF_SIZE`).
  * Responses are constructed using stream buffers (`sbuf_t`) that write directly into the serial driver's pre-allocated ring buffer.

---

## 3. Direct Porting Blueprint for ARIES v3.0 (THEJAS32 / VEGA ET1031)

1. **Zero Upstream Modifications Needed**:
   * `msp_serial.c` is completely architecture-agnostic.
   * As long as our platform layer (`src/platform/`) provides a working `serial_uart.c` adhering to the `serialPort_t` VTable (`serialRead`, `serialWrite`, `serialRxBytesWaiting`, `serialTxBytesFree`), `msp_serial.c` executes out-of-the-box on RISC-V!
2. **Port Assignment**:
   * On ARIES v3.0, we configure `mspConfig()->msp_uart[0] = SERIAL_PORT_USART1` (which maps physically to `UART0` / CP2102N USB bridge).
   * Baud rate: Fixed at `115200`.
   * With this mapping, the instant `mspSerialProcess()` is called in the scheduler, the Betaflight Configurator handshake succeeds immediately!
