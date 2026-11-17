# Knowledge Graph: Historical Planning Mistakes & Architecture Rectification

**Document ID**: `PLAN-MISTAKE-GRAPH-01`  
**Parent Knowledge Graph**: [PORTING_KNOWLEDGE_GRAPH.md](file:///home/ivan/Desktop/betaflight/knowledgebase/PORTING_KNOWLEDGE_GRAPH.md)  
**Detailed Audit Log**: [PLANNING_MISTAKES_ANALYSIS.md](file:///home/ivan/Desktop/betaflight/knowledgebase/PLANNING_MISTAKES_ANALYSIS.md)

---

## 1. The Cascade of Previous Planning Failures

The diagram below illustrates how initial flawed planning assumptions cascaded through compilation, simulation, and flashing to cause the ultimate hardware boot failure in the previous attempt:

```mermaid
graph TD
    subgraph Root_Planning_Mistakes ["Root Planning Mistakes (oldfiles/logs/)"]
        M1["Mistake 1: Assumed SPI Flash XIP @ 0x20000000<br/>(IMPLEMENTATION_PLAN.md)"]
        M2["Mistake 2: Assumed SRAM @ 0x10000000<br/>(Peripheral Bus!) (PLAN_TODO.txt)"]
        M3["Mistake 3: MCU_FLASH_SIZE := 2048<br/>(Unbounded Features) (target.mk)"]
        M4["Mistake 4: Renode Configured with Fake Memory Map<br/>(RISK_ASSESMENT.md)"]
        M5["Mistake 5: Passed Intel .hex to XMODEM Flasher<br/>(IN_SRAM_REMEDIATION_PLAN.md)"]
        M6["Mistake 6: Naive 32-bit mcycle Timebase<br/>(DETAILED_DUMMY_PLAN.md)"]
    end

    subgraph Intermediate_Impacts ["Intermediate Impacts"]
        I1["Linker mapped .text @ 0x20000000 & Stack @ 0x1000FEF0"]
        I2["Binary bloated to 760 KB (OSD, GPS, Blackbox enabled)"]
        I3["Renode Passed 100% of Tests (False Sense of Security)"]
        I4["Flasher wrote ASCII characters (:1000...) into SPI Flash"]
        I5["micros() wrapped every 42.95 seconds -> Scheduler Panic"]
    end

    subgraph Catastrophic_Hardware_Failure ["Hardware Deployment Failure"]
        FAIL1["Bootloader copied only first 250 KB (510 KB Truncated!)"]
        FAIL2["Bootloader jumped to 0x00202000 -> CPU Executed ASCII Garbage"]
        FAIL3["CPU Pushed Stack into Unmapped Peripheral Registers"]
        FAIL4["Permanent Synchronous Trap / Deadlock at Boot!"]
    end

    M1 --> I1
    M2 --> I1
    M3 --> I2
    M4 --> I3
    M5 --> I4
    M6 --> I5

    I2 --> FAIL1
    I4 --> FAIL2
    I1 --> FAIL3
    FAIL1 --> FAIL4
    FAIL2 --> FAIL4
    FAIL3 --> FAIL4
    I5 --> FAIL4

    classDef mistake fill:#7f1d1d,stroke:#ef4444,stroke-width:2px,color:#fff;
    classDef impact fill:#78350f,stroke:#f59e0b,stroke-width:2px,color:#fff;
    classDef fail fill:#450a0a,stroke:#dc2626,stroke-width:3px,color:#fff;

    class M1,M2,M3,M4,M5,M6 mistake;
    class I1,I2,I3,I4,I5 impact;
    class FAIL1,FAIL2,FAIL3,FAIL4 fail;
```

---

## 2. Rectification Matrix: Old Plan vs. New Architecture

The table below maps each identified historical planning mistake to its concrete architectural rectification in our current rebuild knowledge base:

```mermaid
graph LR
    subgraph Old_Flawed_Plan ["Previous Attempt (Failed Plan)"]
        O1["Flash XIP @ 0x20000000"]
        O2["SRAM @ 0x10000000"]
        O3["760 KB Bloated Binary"]
        O4["ASCII .hex to Flasher"]
        O5["32-bit mcycle (43s Rollover)"]
        O6["Address-Bit Shift GPIO"]
    end

    subgraph New_Rebuild_Architecture ["New Rebuild Blueprint (Verified)"]
        N1["Single-Allocator SRAM @ 0x00200000<br/>(linker_ram.lds)"]
        N2["Peripheral Bus Separated<br/>(0x10000000 is APB!)"]
        N3["Modular Sizing < 128 KB<br/>(~72 KB Target Footprint)"]
        N4["Raw .bin Machine Code Deliverable<br/>(flasher stream compatible)"]
        N5["64-bit mtime Up-Counter @ 100 MHz<br/>(>5,800 Year Monotonicity)"]
        N6["Flat 32-bit GPIO Controller @ 0x10000000<br/>(O(1) Array Indexing)"]
    end

    subgraph Verified_Component_Nodes ["Knowledge Nodes"]
        K09["09_BUILD_LINKER_AND_MEMORY.md"]
        K11["11_TARGET_OPTIMIZATION_BUDGET.md"]
        K01["01_SYSTEM_AND_CLOCKS.md"]
        K04["04_GPIO_AND_PIN_IO.md"]
    end

    O1 -. Rectified by .-> N1
    O2 -. Rectified by .-> N2
    O3 -. Rectified by .-> N3
    O4 -. Rectified by .-> N4
    O5 -. Rectified by .-> N5
    O6 -. Rectified by .-> N6

    N1 --> K09
    N2 --> K09
    N3 --> K11
    N4 --> K09
    N5 --> K01
    N6 --> K04
```

---

## 3. Detailed Planning Rectification Catalog

### Rectification 1: Linker Memory Mapping
* **Previous Mistake**: Split memory map (`FLASH 0x20000000` / `RAM 0x10000000`) assuming external XIP flash.
* **Rectified Blueprint**: [09_BUILD_LINKER_AND_MEMORY.md](file:///home/ivan/Desktop/betaflight/knowledgebase/components/09_BUILD_LINKER_AND_MEMORY.md)
* **Concrete Fix**: Single unified memory region `RAM (rwx) : ORIGIN = 0x00200000, LENGTH = 250K`. Code and data are mapped sequentially into internal SRAM, matching the XMODEM bootloader entry point.

---

### Rectification 2: Firmware Sizing & Stripping
* **Previous Mistake**: `MCU_FLASH_SIZE := 2048` enabled all Betaflight features, producing a 760 KB binary that was truncated to 250 KB by the hardware bootloader.
* **Rectified Blueprint**: [11_TARGET_OPTIMIZATION_BUDGET.md](file:///home/ivan/Desktop/betaflight/knowledgebase/components/11_TARGET_OPTIMIZATION_BUDGET.md)
* **Concrete Fix**: Modular feature stripping (`#undef USE_OSD`, `#undef USE_BLACKBOX`, `#undef USE_GPS`, `#undef USE_VCP`). Target footprint is constrained to **~72 KB**, safely below the 128 KB limit and leaving **~178 KB of SRAM free**.

---

### Rectification 3: Hardware Timebase Selection
* **Previous Mistake**: 32-bit `mcycle` reading causing backward time jumps every 42.95 seconds.
* **Rectified Blueprint**: [01_SYSTEM_AND_CLOCKS.md](file:///home/ivan/Desktop/betaflight/knowledgebase/components/01_SYSTEM_AND_CLOCKS.md)
* **Concrete Fix**: Native 64-bit `mtime` up-counter (`0x0200BFF8`) with atomic double-read loop. Provides monotonic microsecond timebase with zero rollover hazards for over 5,800 years.

---

### Rectification 4: GPIO Controller Model
* **Previous Mistake**: Fragmented multi-bank GPIO addressing with address-bit shifts (`0x10080000` / `0x10180000`).
* **Rectified Blueprint**: [04_GPIO_AND_PIN_IO.md](file:///home/ivan/Desktop/betaflight/knowledgebase/components/04_GPIO_AND_PIN_IO.md)
* **Concrete Fix**: Direct control of flat 32-bit GPIO controller at `0x10000000`. Direct array lookup `&ioRecs[pin]` consuming only 256 bytes in SRAM.

---

### Rectification 5: Flashing Protocol & Artifact Generation
* **Previous Mistake**: Passing ASCII Intel `.hex` file to raw XMODEM streaming flasher.
* **Rectified Blueprint**: [09_BUILD_LINKER_AND_MEMORY.md](file:///home/ivan/Desktop/betaflight/knowledgebase/components/09_BUILD_LINKER_AND_MEMORY.md)
* **Concrete Fix**: Build system explicitly generates raw binary image `betaflight_ARIESV3.bin` using `objcopy -O binary`, ensuring only raw executable opcodes are delivered to flash.
