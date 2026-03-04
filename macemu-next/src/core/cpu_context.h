/*
 *  cpu_context.h - Self-contained CPU execution context
 *
 *  Encapsulates all CPU state, memory, and platform interface for a single
 *  emulated CPU instance. Supports both M68K and PPC architectures.
 *
 *  Key features:
 *  - RAII memory management (no leaks)
 *  - Restartable (shutdown() + init() reuses context)
 *  - Thread-safe state transitions
 *  - Architecture-independent API (M68K and PPC use same interface)
 */

#ifndef CPU_CONTEXT_H
#define CPU_CONTEXT_H

#include "sysdeps.h"
#include "platform.h"
#include "emulator_config.h"  // From ../config/
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>

/*
 * CPU execution result codes
 */
enum class CPUExecResult {
    OK,              // Normal execution
    STOPPED,         // CPU stopped by user
    ERROR,           // Execution error
    NOT_INITIALIZED  // Context not initialized
};

/*
 * CPU execution state
 */
enum class CPUState {
    UNINITIALIZED,   // No ROM/CPU loaded
    READY,           // Initialized, ready to run
    RUNNING,         // Currently executing
    PAUSED,          // Paused by user
    ERROR            // Error state
};

/*
 * CPUContext - Self-contained CPU execution context
 *
 * This class encapsulates everything needed to run an emulated CPU:
 * - Memory (RAM, ROM) with RAII management
 * - Platform interface (CPU backend, drivers)
 * - Execution state and synchronization
 * - Mac subsystems (XPRAM, disks, etc.)
 *
 * The same API works for both M68K and PPC, making it easy to
 * switch architectures at runtime.
 *
 * Usage:
 *   CPUContext ctx;
 *   if (ctx.init_m68k(config)) {
 *       ctx.execute_loop();  // Runs until stopped
 *   }
 *   ctx.shutdown();  // Clean up (automatic on destruction)
 */
class CPUContext {
public:
    CPUContext();
    ~CPUContext();

    // ========================================
    // Initialization
    // ========================================

    /*
     * Initialize for M68K emulation
     *
     * Allocates memory, loads ROM, initializes CPU backend, patches ROM,
     * and sets up Mac subsystems.
     *
     * Thread-safe: Can be called from any thread.
     * Idempotent: Calling again will shutdown first.
     *
     * Returns: true on success, false on error
     */
    bool init_m68k(const config::EmulatorConfig& config);

    /*
     * Initialize for PPC emulation (STUB - not yet implemented)
     *
     * Future: Will initialize SheepShaver-style PPC emulation.
     *
     * Returns: false (not implemented)
     */
    bool init_ppc(const config::EmulatorConfig& config);

    /*
     * Shutdown CPU context
     *
     * Stops execution, cleans up Mac subsystems, and frees memory.
     * Safe to call multiple times (idempotent).
     * Automatically called by destructor.
     *
     * Thread-safe: Can be called from any thread.
     */
    void shutdown();

    /*
     * Check if initialized
     */
    bool is_initialized() const {
        return state_.load() != CPUState::UNINITIALIZED;
    }

    // ========================================
    // Execution Control
    // ========================================

    /*
     * Execute CPU instructions
     *
     * Runs the CPU execution loop until:
     * - User calls stop()
     * - Error occurs
     * - Context is not initialized
     *
     * This is the main execution function, called by the CPU thread.
     * It's a blocking call that only returns when execution stops.
     *
     * Returns: Reason for stopping
     */
    CPUExecResult execute_loop();

    /*
     * Execute one instruction (for testing/debugging)
     *
     * Returns: OK on success, error code on failure
     */
    CPUExecResult execute_one();

    /*
     * Stop CPU execution
     *
     * Signals the execution loop to stop gracefully.
     * Non-blocking: Returns immediately, execution stops asynchronously.
     *
     * Thread-safe: Can be called from any thread.
     */
    void stop();

    /*
     * Pause CPU execution
     *
     * Pauses the CPU but keeps it initialized.
     * Can be resumed with resume().
     *
     * Thread-safe: Can be called from any thread.
     */
    void pause();

    /*
     * Resume CPU execution
     *
     * Resumes a paused CPU.
     *
     * Thread-safe: Can be called from any thread.
     */
    void resume();

    /*
     * Reset CPU to ROM entry point
     *
     * Resets program counter and CPU state, but keeps memory intact.
     *
     * Thread-safe: Can be called from any thread (stops execution first).
     */
    void reset();

    /*
     * Check execution state
     */
    CPUState get_state() const {
        return state_.load();
    }

    bool is_running() const {
        return state_.load() == CPUState::RUNNING;
    }

    bool is_paused() const {
        return state_.load() == CPUState::PAUSED;
    }

    // ========================================
    // Memory Access (for debugging/inspection)
    // ========================================

    uint8_t* get_ram() const { return ram_.get(); }
    uint8_t* get_rom() const { return rom_.get(); }
    uint32_t get_ram_size() const { return ram_size_; }
    uint32_t get_rom_size() const { return rom_size_; }

    // ========================================
    // Platform Access (for drivers)
    // ========================================

    Platform* get_platform() { return &platform_; }
    const Platform* get_platform() const { return &platform_; }

    // ========================================
    // Architecture Info
    // ========================================

    config::Architecture get_architecture() const {
        return architecture_;
    }

    const char* get_architecture_string() const {
        return (architecture_ == config::Architecture::M68K) ? "m68k" : "ppc";
    }

private:
    // ========================================
    // Internal State
    // ========================================

    // Architecture
    config::Architecture architecture_;

    // Memory (RAII - auto-freed on destruction)
    std::unique_ptr<uint8_t[]> ram_;
    std::unique_ptr<uint8_t[]> rom_;
    std::unique_ptr<uint8_t[]> scratch_mem_;
    uint32_t ram_size_;
    uint32_t rom_size_;

    // Platform interface (CPU backend + drivers)
    Platform platform_;

    // CPU configuration
    int cpu_type_;
    int fpu_type_;
    bool twenty_four_bit_;

    // Execution state
    std::atomic<CPUState> state_;
    std::mutex mutex_;
    std::condition_variable cv_;

    // ========================================
    // Internal Methods
    // ========================================

    /*
     * Load ROM file into memory
     *
     * Returns: true on success, false on error
     */
    bool load_rom(const char* rom_path);

    /*
     * Initialize Mac subsystems (XPRAM, drivers, audio, video, etc.)
     *
     * Must be called after ROM is loaded but before CPU init.
     *
     * Returns: true on success, false on error
     */
    bool init_mac_subsystems();

    /*
     * Set CPU state (internal, not thread-safe)
     */
    void set_state(CPUState new_state) {
        state_.store(new_state);
        cv_.notify_all();
    }

    // Non-copyable (memory is unique)
    CPUContext(const CPUContext&) = delete;
    CPUContext& operator=(const CPUContext&) = delete;
};

#endif // CPU_CONTEXT_H
