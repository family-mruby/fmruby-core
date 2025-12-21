# IPC Graphics Communication Crash Investigation

## Date
2025-11-04

## Issue Summary
Segmentation fault occurring during graphics IPC communication between fmruby-core (Core) and SDL2 Host process on Linux target.

## System Architecture

### Components
- **Core (fmruby-core.elf)**: Embedded Ruby runtime on FreeRTOS POSIX implementation
- **Host (SDL2 process)**: Graphics/audio rendering process
- **IPC**: Unix domain socket at `/tmp/fmrb_socket`
- **Protocol**: msgpack + COBS encoding + CRC32

### Communication Flow
```
Core (FreeRTOS task)
  -> fmrb_gfx API
  -> fmrb_link_transport
  -> fmrb_hal_link (POSIX)
  -> Unix socket
  -> Host SDL2 process
  -> ACK response
```

## Crash Details

### GDB Stack Trace
```
Thread 5 "fmruby-core.elf" received signal SIGSEGV, Segmentation fault.
xQueueSemaphoreTake (xQueue=0x0, ...)

#0  xQueueSemaphoreTake (xQueue=0x0, ...) at queue.c:1731
#1  fmrb_malloc (handle=0, size=13) at fmrb_alloc.c:149
#2  fmrb_sys_malloc (size=13) at fmrb_alloc.c:298
#3  fmrb_hal_link_send (...) at fmrb_hal_link_posix.c:253
#4  send_raw_message (...) at fmrb_link_transport.c:128
#5  fmrb_link_transport_send (...) at fmrb_link_transport.c:186
#6  send_graphics_command (...) at fmrb_gfx.c:39
#7  fmrb_gfx_clear (...) at fmrb_gfx.c:175
#8  init_gfx_audio () at host_task.c:98
#9  fmrb_host_task (...) at host_task.c:180
```

### Direct Cause
- `node->mutex` is NULL (xQueue=0x0)
- Occurs in `MUTEX_LOCK(node->mutex)` at fmrb_alloc.c:149
- Called from `fmrb_sys_malloc()` during message buffer allocation

### Timing
Crash occurs immediately after:
1. RX thread creation via `pthread_create()` (LWP 103)
2. mruby tick task creation (LWP 104)
3. First `fmrb_sys_malloc()` call in host task

## Root Cause Analysis

### Memory Allocator State
- **Initialization**: System memory pool (handle=0) was successfully initialized
  ```
  I (96307981) fmrb_alloc: Created pool handle=0, size=511960
  I (96307981) fmrb_alloc: System mem allocator initialized. Handle = 0
  ```
- **Mutex Creation**: `MUTEX_INIT(node->mutex)` succeeded during initialization
- **Corruption**: `node->mutex` becomes NULL after thread/task creation

### Suspected Cause: Memory Corruption
The most likely cause is **memory pool structure corruption** due to:

1. **FreeRTOS POSIX + pthread interaction**
   - FreeRTOS tasks and pthreads share memory space
   - Thread/task stack allocation may overlap with memory pool metadata

2. **TLSF Pool Node Location**
   - Pool node structure is placed at the beginning of memory pool
   - Thread creation immediately after may overwrite this area

3. **Timing Evidence**
   - First 3 messages succeed (before RX thread creation)
   - Crash occurs after pthread_create() for RX thread
   - Crash on first malloc attempt after thread creation

## Investigation Steps Taken

### 1. Initial Debugging
- Added extensive logging to all layers:
  - `fmrb_hal_link_send()`: Entry, buffer allocation, COBS encoding
  - `fmrb_link_transport_send()`: Message sequencing, pending list
  - `fmrb_gfx`: Command dispatch
  - Host SDL2: Message reception, ACK transmission

### 2. Socket Communication
- Verified non-blocking socket mode on Core side
- Confirmed ACK messages sent successfully from Host
- Issue: Core never receives ACK (RX thread problem suspected)

### 3. Memory Allocator Investigation
- Confirmed `fmrb_mem_create_handle()` succeeds
- Traced mutex initialization in `MUTEX_INIT()`
- Discovered mutex corruption after thread creation

## Code Changes Made (Investigation Phase)

### File: `main/lib/fmrb_hal/platform/posix/fmrb_hal_link_posix.c`

#### Changes:
1. **Non-blocking socket mode**
   ```c
   // Set socket to non-blocking mode
   int flags = fcntl(global_socket_fd, F_GETFL, 0);
   fcntl(global_socket_fd, F_SETFL, flags | O_NONBLOCK);
   ```

2. **RX thread improvements**
   - Added ready notification log
   - Improved error handling for EAGAIN/EWOULDBLOCK
   - Added receive queue for message buffering

3. **Extensive logging** (ALL ADDED LOGS ARE FOR DEBUG ONLY)
   ```c
   ESP_LOGI(TAG, "fmrb_hal_link_send: START channel=%d, msg=%p, size=%zu", ...);
   ESP_LOGI(TAG, "fmrb_hal_link_send: link_initialized=%d, global_socket_fd=%d", ...);
   ESP_LOGI(TAG, "fmrb_hal_link_send: acquiring socket_mutex");
   ESP_LOGI(TAG, "fmrb_hal_link_send: Allocated buffer %zu bytes", ...);
   ESP_LOGI(TAG, "fmrb_hal_link_send: COBS encoded %zu -> %zu bytes", ...);
   ESP_LOGI(TAG, "fmrb_hal_link_send: Calling send() with %zu bytes", ...);
   ESP_LOGI(TAG, "fmrb_hal_link_send: SUCCESS, returning FMRB_OK");
   ```

4. **Attempted malloc replacement** (REVERTED - DO NOT USE)
   - Temporarily replaced `fmrb_sys_malloc()` with `malloc()`
   - Added static TX buffers
   - **This was incorrect approach and should be reverted**

### File: `host/sdl2/src/socket_server.c`

#### Changes:
1. **Host-side logging**
   ```c
   printf("Host: Received %zd bytes from Core\n", bytes_read);
   printf("Host: ACK sent for seq=%u, status=%u (%zd bytes)\n", ...);
   ```

2. **Error message improvements**
   ```c
   fprintf(stderr, "Host: Failed to send ACK: sent=%zd, expected=%zd\n", ...);
   fprintf(stderr, "Host: Read error: %s\n", strerror(errno));
   ```

### File: `main/lib/fmrb_link/fmrb_link_transport.c`

#### Changes:
1. **Removed problematic delay**
   - Removed 20ms initial delay before first message
   - This delay was causing timing issues with tick restrictions (MRB_TICK_UNIT=5)

2. **Temporarily disabled ACK polling loop**
   ```c
   // TODO: Implement ACK polling loop
   // For now, skip polling to avoid delay-related issues
   ```

3. **Enhanced logging**
   ```c
   FMRB_LOGI(TAG, "About to add message to pending list: seq=%u", sequence);
   FMRB_LOGI(TAG, "Message seq=%u added to pending list, count=%d", ...);
   FMRB_LOGI(TAG, "fmrb_link_transport_send returning OK");
   ```

## Logs Analysis

### Successful Execution (Before Crash)
```
I (96307984) host: ============================== gfx demo ==========================
I (96307984) fmrb_hal_link: fmrb_hal_link_send: START channel=0, msg=0x..., size=9
I (96307984) fmrb_hal_link: fmrb_hal_link_send: link_initialized=1, global_socket_fd=3
[New Thread 0x74812e6bd6c0 (LWP 103)]  <-- RX thread created
I (96307987) fmrb_hal_link: RX thread for channel 0 started and ready
I (96307988) fmrb_hal_link: Started receive thread for channel 0
I (96307988) fmrb_hal_link: fmrb_hal_link_send: acquiring socket_mutex
I (96307988) fmrb_hal_link: fmrb_hal_link_send: socket_mutex acquired
Client connected
I (96307988) hal: hal_init called (FreeRTOS mode)
[New Thread 0x74812debc6c0 (LWP 104)]  <-- mruby tick task created

Thread 5 received signal SIGSEGV  <-- CRASH HERE
```

### Key Observations
1. Socket connection and mutex acquisition succeed
2. RX thread starts successfully
3. Crash occurs immediately after mruby tick task creation
4. No logs from buffer allocation in `fmrb_sys_malloc()`

## Remaining Issues

### Unsolved Problems
1. **Memory corruption mechanism**: Exact cause of `node->mutex` becoming NULL
2. **ACK reception**: RX thread never logs receiving ACK messages
3. **Thread safety**: Interaction between FreeRTOS POSIX and pthread

### Questions
1. Is TLSF pool node placement at memory pool start safe with thread stacks?
2. Should memory pools use separate allocation for metadata?
3. Do pthread and FreeRTOS task share stack space that could overlap?

## Next Steps (Proposed)

### Investigation
1. Add memory guard patterns around pool node structure
2. Log pool node address and mutex pointer before/after thread creation
3. Check if thread stack allocation overlaps with memory pool region
4. Verify TLSF metadata integrity

### Potential Fixes
1. **Separate metadata allocation**: Allocate pool node outside of managed memory
2. **Memory protection**: Use mprotect() on pool metadata region (Linux only)
3. **Alternative allocator**: For pthread context, use different allocation strategy
4. **Stack guard**: Ensure thread stacks don't overlap with memory pools

## Configuration Details

### FreeRTOS Settings
- `configTICK_RATE_HZ = 1000` (1ms tick)
- `MRB_TICK_UNIT = 5` (minimum delay 5ms)
- `configTOTAL_HEAP_SIZE = 66560`

### Memory Pools
- System pool: 511960 bytes (POOL_ID_SYSTEM)
- Uses TLSF (Two Level Segregated Fit) allocator
- Pool node structure at start of pool memory

### IPC Protocol
- Message format: `[type, seq, sub_cmd, payload]` (msgpack)
- COBS encoding with 0x00 terminator
- CRC32 for integrity
- Max payload: 4096 bytes
- Pending messages: 8 max (MAX_PENDING_MESSAGES)

## Conclusion

The crash is caused by memory corruption of the TLSF pool node structure, specifically the mutex field becoming NULL. This occurs after pthread creation for RX thread, suggesting interaction between FreeRTOS POSIX implementation and pthread memory management.

The investigation revealed the crash mechanism but not the complete root cause. Further investigation into memory layout and thread stack allocation is required.

All code changes made during investigation are **temporary debugging aids** and should not be considered final solutions. The malloc() replacement attempt was particularly incorrect and has been reverted.
