#pragma once

#include "../util/config/config.h"
#include "dxvk_include.h"

#include "../vulkan/vulkan_loader.h"

namespace dxvk {

  struct DxvkOptions {
    DxvkOptions() { }
    DxvkOptions(const Config& config);

    /// Enable debug utils
    bool enableDebugUtils = false;

    /// Enable memory defragmentation
    Tristate enableMemoryDefrag = Tristate::Auto;

    /// Number of compiler threads
    /// when using the state cache
    int32_t numCompilerThreads = 0;

    /// Enable graphics pipeline library
    Tristate enableGraphicsPipelineLibrary = Tristate::Auto;

    /// Enable descriptor buffer
    Tristate enableDescriptorBuffer = Tristate::Auto;

    /// Enables pipeline lifetime tracking
    Tristate trackPipelineLifetime = Tristate::Auto;

    // Enable async pipelines
    bool enableAsync;
    // Enable state cache with gpl and fixes for async
    bool gplAsyncCache;

    /// Shader-related options
    Tristate useRawSsbo = Tristate::Auto;

    /// HUD elements
    std::string hud;

    /// Forces swap chain into MAILBOX (if true)
    /// or FIFO_RELAXED (if false) present mode
    Tristate tearFree = Tristate::Auto;

    /// Enables latency sleep
    /// Defaults to false in this build to activate the FramePacer,
    /// especially for the case when the game doesn't support Reflex
    Tristate latencySleep = Tristate::False;

    /// Latency tolerance, in microseconds
    int32_t latencyTolerance = 0u;

    /// Disable VK_NV_low_latency2. This extension
    /// appears to be all sorts of broken on 32-bit.
    Tristate disableNvLowLatency2 = Tristate::Auto;

    // Hides integrated GPUs if dedicated GPUs are
    // present. May be necessary for some games that
    // incorrectly assume monitor layouts.
    bool hideIntegratedGraphics = false;

    /// Clears all mapped memory to zero.
    bool zeroMappedMemory = false;

    /// Allows full-screen exclusive mode on Windows
    bool allowFse = false;

    /// Whether to enable tiler optimizations
    Tristate tilerMode = Tristate::Auto;

    /// Frame pacing
    std::string framePace;

    /// A value in microseconds to fine-tune the low-latency frame pacing.
    /// Positive values make a frame begin later which might improve responsiveness.
    /// Negative values make a frame begin earlier which might improve fps.
    int32_t lowLatencyOffset;

    /// Determines whether a frame is allowed to begin before finishing processing
    /// the cpu-part of the previous one, when low-latency frame pacing is used.
    bool lowLatencyAllowCpuFramesOverlap;

    /// Overrides memory budget for DXVK
    VkDeviceSize maxMemoryBudget = 0u;

    // Device name
    std::string deviceFilter;
  };

}
