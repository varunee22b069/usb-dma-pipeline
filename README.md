# USB-DMA Based Data Acquisition and Processing Pipeline

This project implements a **high-throughput**, **reliable**, and **CPU-efficient** data pipeline to acquire, process, and store continuous ADC data from a Tiva TM4C123GXL microcontroller over USB.

---

## Main Goals
- **High Throughput:** Utilize DMA and double buffering to achieve near-sustained USB full-speed rates.  
- **CPU Offloading:** Minimize CPU intervention during acquisition and transfer.  
- **Reliability:** Ensure continuous operation with robust synchronization and loss tracking.

---

## What does it do?

### On the MCU
- The MCU (Tiva TM4C123GXL) continuously samples analog data using **ADC0 sequencer 3**.  
- **DMA** (Direct Memory Access) transfers ADC results directly into preallocated buffers, avoiding CPU-intensive copy operations.  
- A **Ping-Pong DMA** setup (double buffering) ensures that while one buffer is being transmitted over USB, the next buffer is being filled by DMA.  
- Each filled buffer is transmitted to the host via the **USB FS peripheral** (12 Mbps).  
- The next DMA transfer is rearmed before the current USB transmission finishes, keeping the pipeline active with minimal idle time.  
- CPU activity on the MCU is limited to DMA rearming and initialization, ensuring maximum sampling efficiency.

---

### In the Kernel Space
Currently, communication with the MCU is handled using **libusb** from userspace.  
A **custom kernel driver** (planned integration) will later handle:  
- DMA-coherent buffer allocation and mmap to userspace  
- High-speed bulk transfer management  
- Synchronization via `poll()` and `ioctl()`  
This will further reduce latency and improve throughput consistency.

---

### In the Userspace
The userspace program (written in C/C++ with optional Python visualization) orchestrates the data pipeline using multiple threads for concurrent processing.

#### **Main Thread**
- Handles **USB data acquisition** via `libusb_bulk_transfer()`.  
- Continuously writes incoming packets into **double buffers** shared with processing threads.  
- Tracks throughput, packet integrity, and dropped samples in real time.  
- Operates independently — it never blocks on FFT or logging tasks, ensuring uninterrupted data flow.

#### **FFT Processor Thread**
- Waits for a full buffer using condition variables.  
- Converts raw ADC samples to voltage and performs an **FFT** using **FFTW3**.  
- Extracts and streams the **top-K frequency components** to a Python plotting process (`plotter.py`) for live visualization.  
- Releases the buffer once processing is complete.

#### **Logger Thread**
- Receives filled buffers from the acquisition thread.  
- Logs raw binary data with lightweight headers (marker, sequence number, size).  
- Automatically rolls over to a new file when the current log exceeds 1 GB.  
- Ensures minimal blocking through independent file I/O.

#### **Compressor Thread**
- Waits for log rollover signals.  
- Compresses completed log files asynchronously using `gzip -9`.  
- Keeps storage usage bounded while maintaining near-real-time logging.

---

## Summary
This pipeline demonstrates a **complete high-performance data acquisition stack**, covering:
- Real-time ADC sampling via DMA  
- USB-based high-speed data transport  
- Multithreaded processing and logging  
- Efficient data compression and visualization  

The modular design allows future integration of a **custom Linux USB driver** and hardware timestamp synchronization for deterministic performance measurement.
