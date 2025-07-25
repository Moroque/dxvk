#include "dxvk_options.h"

namespace dxvk {

  DxvkOptions::DxvkOptions(const Config& config) {
    if (env::getEnvVar("DXVK_GPLASYNCCACHE") == "1")
      gplAsyncCache = true;
    else
      gplAsyncCache = config.getOption<bool>("dxvk.gplAsyncCache", false);
    
    if (env::getEnvVar("DXVK_ASYNC") == "0")
      enableAsync = false;
    else
      enableAsync = config.getOption<bool>("dxvk.enableAsync", true);
    
    enableDebugUtils      = config.getOption<bool>    ("dxvk.enableDebugUtils",       false);
    enableMemoryDefrag    = config.getOption<Tristate>("dxvk.enableMemoryDefrag",     Tristate::Auto);
    numCompilerThreads    = config.getOption<int32_t> ("dxvk.numCompilerThreads",     0);
    enableGraphicsPipelineLibrary = config.getOption<Tristate>("dxvk.enableGraphicsPipelineLibrary", Tristate::Auto);
    enableDescriptorBuffer = config.getOption<Tristate>("dxvk.enableDescriptorBuffer",Tristate::True);
    trackPipelineLifetime = config.getOption<Tristate>("dxvk.trackPipelineLifetime",  Tristate::Auto);
    useRawSsbo            = config.getOption<Tristate>("dxvk.useRawSsbo",             Tristate::Auto);
    hud                   = config.getOption<std::string>("dxvk.hud", "");
    tearFree              = config.getOption<Tristate>("dxvk.tearFree",               Tristate::False);
    latencySleep          = config.getOption<Tristate>("dxvk.latencySleep",           Tristate::False);
    latencyTolerance      = config.getOption<int32_t> ("dxvk.latencyTolerance",       1000);
    disableNvLowLatency2  = config.getOption<Tristate>("dxvk.disableNvLowLatency2",   Tristate::Auto);
    hideIntegratedGraphics = config.getOption<bool>   ("dxvk.hideIntegratedGraphics", false);
    zeroMappedMemory      = config.getOption<bool>    ("dxvk.zeroMappedMemory",       false);
    allowFse              = config.getOption<bool>    ("dxvk.allowFse",               false);
    framePace             = config.getOption<std::string>("dxvk.framePace",           "");
    lowLatencyOffset      = config.getOption<int32_t> ("dxvk.lowLatencyOffset",       0);
    lowLatencyAllowCpuFramesOverlap
                          = config.getOption<bool>    ("dxvk.lowLatencyAllowCpuFramesOverlap", true);
    deviceFilter          = config.getOption<std::string>("dxvk.deviceFilter",        "");
    tilerMode             = config.getOption<Tristate>("dxvk.tilerMode",              Tristate::Auto);

    auto budget = config.getOption<int32_t>("dxvk.maxMemoryBudget", 0);
    maxMemoryBudget = VkDeviceSize(std::max(budget, 0)) << 20u;
  }

}
