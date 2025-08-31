#pragma once

#include "dxvk_framepacer_mode.h"
#include "dxvk_latency_stats.h"
#include "dxvk_gpu_progress.h"
#include "../dxvk_options.h"
#include "../../util/log/log.h"
#include "../../util/util_string.h"

namespace dxvk {

  /*
   * This low-latency mode aims to reduce latency with minimal impact in fps.
   * Effective when operating in the GPU-limit. Efficient to be used in the CPU-limit as well.
   *
   * Greatly reduces input lag variations when switching between CPU- and GPU-limit, and
   * compared to the max-frame-latency approach, it has a much more stable input lag when
   * GPU running times change dramatically, which can happen for example when rotating within a scene.
   *
   * The current implementation rather generates fluctuations alternating frame-by-frame
   * depending on the game's and dxvk's CPU-time variations. This might be visible as a loss
   * in smoothness in games which provide strongly varying frame times. Most games should be fine,
   * but for example in 'God of War', we measured a prediction error of +/- 3 ms for the
   * 99% percentiles and up to +/- 2 ms for the 95% percentiles, which is a bit too much.
   * Smoothing may be a consideration in such a case, but unsuitable smoothing will degrade input feel,
   * so it's not implemented for now, but more advanced smoothing techniques will be investigated
   * in the future.
   *
   * In some situations however, this low-latency pacing actually presents smoother visuals
   * than max-frame-latency, because when the prediction error is small, this jitter effect
   * gets negligible, and it becomes apparent that the generated video progresses more cleanly
   * in time with regards to medium-term time consistency. In other words, the video playback speed
   * is more accurate and steady - for the same reasons why input lag consistency is improved.
   *
   * Fps limiting is tightly integrated into the frame pacing logic and is highly recommended
   * to be used in place of most ingame limiters.
   *
   * A VRR mode that activates V-Sync is also provided to combine low latency with image clarity.
   * It's achieved by predictively limiting at the present timeline, respecting v-blanks to
   * avoid going into V-Sync buffering. This can further be tuned by using the fps limiter at the
   * same time, which means there are basically two limiters active. The advantage of doing so
   * is that the "normal" fps limiter isn't affected by prediction errors. Selecting a VRR refresh
   * rate smaller than the monitor's refresh rate will lower the chance and/or lower the duration
   * frames will go into V-Sync buffering.
   *
   * It further can be fine-tuned via the dxvk.lowLatencyOffset and
   * dxvk.lowLatencyAllowCpuFramesOverlap config variables.
   * Compared to maxFrameLatency = 3, render-latency reductions of up to 67% are achieved.
   */

  class LowLatencyMode : public FramePacerMode {
    using microseconds = std::chrono::microseconds;
    using time_point = high_resolution_clock::time_point;
  public:

    LowLatencyMode(Mode mode, LatencyMarkersStorage* storage, const DxvkOptions& options, uint64_t firstFrameId, int refreshRate = 0)
    : FramePacerMode(mode, storage, firstFrameId),
      m_lowLatencyOffset(getLowLatencyOffset(options)),
      m_allowCpuFramesOverlap(options.lowLatencyAllowCpuFramesOverlap),
      m_presentationStats(5000),
      m_gpuProgress(storage) {
      Logger::info( str::format("  lowLatencyOffset: ", m_lowLatencyOffset) );
      Logger::info( str::format("  lowLatencyAllowCpuFramesOverlap: ", m_allowCpuFramesOverlap) );

      if (refreshRate > 0) {
        m_vrrRefreshInterval = 1'000'000 / refreshRate;
        Logger::info( str::format("  vrr refresh rate: ", refreshRate) );
      }

    }

    ~LowLatencyMode() {}

    bool getDesiredPresentMode( uint32_t& presentMode ) const override;

    void startFrame( uint64_t frameId ) override {

      using std::chrono::duration_cast;

      if (!m_allowCpuFramesOverlap)
        m_fenceCsFinished.wait( frameId-1 );

      m_fenceGpuStart.wait( frameId-1 );

      if (m_mode == LOW_LATENCY_VRR)
        m_fenceFrameFinished.wait( frameId-2 );

      time_point now = high_resolution_clock::now();
      const LatencyMarkers* m = m_latencyMarkersStorage->getConstMarkers(frameId-1);
      uint64_t finishedId = m_latencyMarkersStorage->getTimeline()->gpuFinished.load();
      if (finishedId <= m_firstFrameId+1)
        return;

      if (finishedId == frameId-1) {
        // we are the only in-flight frame
        int32_t delay = getFpsLimiterDelay( m, now );
        sleepFor( now, delay );
        return;
      }

      SyncProps props = getSyncPrediction();
      int32_t targetGpuTime = props.optimizedGpuTime - props.cpuUntilGpuStart + m_lowLatencyOffset;

      if (m_mode == LOW_LATENCY_VRR) {
        int32_t vrrDelay = getVrrDelay( frameId, props, now );
        int32_t vrrGpuTime = vrrDelay - std::max( m->gpuStart, props.cpuUntilGpuStart );
        targetGpuTime = std::max( targetGpuTime, vrrGpuTime );
      }

      if (targetGpuTime <= 0) {
        // we don't track gpu progress, because we start earlier than
        // when the first submission might be processed by the gpu.
        // this means, this path is mostly taken when being cpu limited.
        int32_t gpuDelay = getGpuDelay( props, m, now );
        int32_t delay = std::max( gpuDelay, getCpuDelay( props, m, now ) );
        delay = std::max( delay, getFpsLimiterDelay( m, now ) );
        delay = std::max( delay, getVrrDelay( frameId, props, now ) );

        sleepFor( now, delay );
        return;
      }

      time_point lastFrameFinish;

      if (targetGpuTime < props.optimizedGpuTime) {
        m_gpuProgress.waitUntil( frameId-1, targetGpuTime, props.cpuUntilGpuStart );
        lastFrameFinish = high_resolution_clock::now()
          + microseconds(props.optimizedGpuTime - targetGpuTime);
      }
      else {
        if (m_mode == LOW_LATENCY_VRR) {
          m_fenceFrameFinished.wait( frameId-1 );
        } else {
          m_fenceGpuFinished.wait( frameId-1 );
          lastFrameFinish = high_resolution_clock::now();
        }
      }

      props = getSyncPrediction();
      now = high_resolution_clock::now();
      int32_t cpuDelay = getCpuDelay( props, m, now );
      int32_t delay = std::max( cpuDelay, getFpsLimiterDelay( m, now ) );
      delay = std::max( delay, getVrrDelay( frameId, props, now, lastFrameFinish ) );
      sleepFor( now, delay );

    }


    void notifyGpuReady( uint64_t frameId, time_point t ) override
      { m_gpuProgress.notifyGpuReady( frameId, t ); }

    void notifyQueueSubmit( uint64_t frameId, time_point t ) override
      { m_gpuProgress.notifyQueueSubmit( frameId, t ); }


    void finishRender( uint64_t frameId ) override {

      using std::chrono::duration_cast;
      m_gpuProgress.finishRender( frameId );
      const LatencyMarkers* m = m_latencyMarkersStorage->getConstMarkers(frameId);

      int32_t numLoop = (int32_t)(m->gpuReady.size())-1;
      if (numLoop <= 1) {
        m_props[frameId % m_props.size()] = SyncProps();
        m_props[frameId % m_props.size()].isOutlier = true;
        m_propsFinished.store( frameId );
        return;
      }

      // estimates the optimal overlap for cpu/gpu work by optimizing gpu scheduling first
      // such that the gpu doesn't go into idle for this frame, and then aligning cpu submits
      // where gpuSubmit[i] <= gpuRun[i] for all i

      std::vector<int32_t>& gpuRun = m_tempGpuRun;
      gpuRun.clear();
      int32_t optimizedGpuTime = 0;
      gpuRun.push_back(optimizedGpuTime);

      for (int i=0; i<numLoop; ++i) {
        time_point _gpuRun = std::max( m->gpuReady[i], m->gpuQueueSubmit[i] );
        int32_t duration = duration_cast<microseconds>( m->gpuReady[i+1] - _gpuRun ).count();
        optimizedGpuTime += duration;
        gpuRun.push_back(optimizedGpuTime);
      }

      int32_t alignment = duration_cast<microseconds>( m->gpuSubmit[numLoop-1] - m->gpuSubmit[0] ).count()
        - gpuRun[numLoop-1];

      int32_t offset = 0;
      for (int i=numLoop-2; i>=0; --i) {
        int32_t curSubmit = duration_cast<microseconds>( m->gpuSubmit[i] - m->gpuSubmit[0] ).count();
        int32_t diff = curSubmit - gpuRun[i] - alignment;
        diff = std::max( 0, diff );
        offset += diff;
        alignment += diff;
      }


      SyncProps& props = m_props[frameId % m_props.size()];
      props.gpuSync = gpuRun[numLoop-1];
      props.cpuUntilGpuSync = offset + duration_cast<microseconds>( m->gpuSubmit[numLoop-1] - m->start ).count();
      props.cpuUntilGpuStart = props.cpuUntilGpuSync - props.gpuSync;
      props.optimizedGpuTime = optimizedGpuTime;
      props.csStart = m->csStart;
      props.csFinished = m->csFinished;
      props.isOutlier = isOutlier(frameId);

      m_propsFinished.store( frameId );

    }


    void endFrame( uint64_t frameId ) override {

      if (m_mode == LOW_LATENCY_VRR && frameId > m_firstFrameId+1) {
        const LatencyMarkers* m1 = m_latencyMarkersStorage->getConstMarkers(frameId-1);
        const LatencyMarkers* m2 = m_latencyMarkersStorage->getConstMarkers(frameId);

        int32_t gpuFinishedInterval = std::chrono::duration_cast<microseconds>(
          (m2->start + microseconds(m2->gpuFinished)) - (m1->start + microseconds(m1->gpuFinished))).count();

        // only push values where we probably weren't running into v-sync buffering.
        // otherwise we can get a presentation stats median drift due to feedback loop.
        if (gpuFinishedInterval >= 0.99 * m_vrrRefreshInterval)
          m_presentationStats.push( m2->end, m2->presentFinished - m2->gpuFinished );
      }

    }



  private:

    struct SyncProps {
      int32_t optimizedGpuTime;   // gpu executing packed submits in one go
      int32_t gpuSync;            // gpuStart to this sync point, in microseconds
      int32_t cpuUntilGpuSync;
      int32_t cpuUntilGpuStart;
      int32_t csStart;
      int32_t csFinished;
      bool    isOutlier;
    };


    SyncProps getSyncPrediction() const {
      // In the future we might use more samples to get a prediction.
      // Possibly this will be optional, as until now, basing it on
      // just the previous frame gave us the best mouse input feel.
      // Simple averaging or median filtering is surely not the way
      // to go, but more advanced methods will be investigated.
      // Outlier removal has worked out really well though, so that's
      // what we are using here.

      SyncProps res = {};
      uint64_t id = m_propsFinished;
      if (id < m_firstFrameId+7)
        return res;

      for (size_t i=0; i<7; ++i) {
        const SyncProps& props = m_props[ (id-i) % m_props.size() ];
        if (!props.isOutlier) {
          id = id-i;
          break;
        }
      }

      return m_props[ id % m_props.size() ];

    };


    bool isOutlier( uint64_t frameId ) const {

      constexpr size_t numLoop = 7;
      int32_t totalCpuTime = 0;
      for (size_t i=1; i<numLoop; ++i) {
        const LatencyMarkers* m = m_latencyMarkersStorage->getConstMarkers(frameId-i);
        totalCpuTime += m->cpuFinished;
      }

      int32_t avgCpuTime = totalCpuTime / (numLoop-1);
      const LatencyMarkers* m = m_latencyMarkersStorage->getConstMarkers(frameId);
      if (m->cpuFinished > 1.3*avgCpuTime || m->gpuSubmit.empty() || m->gpuReady.size() != (m->gpuSubmit.size()+1) )
        return true;

      return false;

    }


    int32_t getGpuDelay( const SyncProps& props, const LatencyMarkers* m, time_point now ) const {

      int32_t lastFrameStart = std::chrono::duration_cast<microseconds>( m->start - now ).count();
      int32_t gpuReadyPrediction = lastFrameStart
        + std::max( props.cpuUntilGpuStart, m->gpuStart )
        + props.optimizedGpuTime;

      int32_t gpuDelay = gpuReadyPrediction - props.cpuUntilGpuStart;
      return gpuDelay + m_lowLatencyOffset;

    }


    int32_t getCpuDelay( const SyncProps& props, const LatencyMarkers* m, time_point now ) const {

      if (!m_allowCpuFramesOverlap)
        return 0;

      // prevents the cs thread from creating additional latency.
      // in the future, we should handle this a bit more sophisticatedly, allowing more overlap.
      int32_t cpuReadyPrediction = std::chrono::duration_cast<microseconds>(
          m->start + microseconds(props.csFinished) - now).count();
      int32_t cpuDelay = cpuReadyPrediction - props.csStart;
      return cpuDelay + m_lowLatencyOffset;

    }


    int32_t getFpsLimiterDelay( const LatencyMarkers* m, time_point now ) const {

      int32_t frametime = std::chrono::duration_cast<microseconds>( now - m->start ).count();
      return std::max( 0, m_fpsLimitFrametime.load() - frametime );

    }


    int32_t getVrrDelay( uint64_t frameId, const SyncProps& props, const time_point& now, const time_point& lastFrameFinish = time_point{} ) {

      if (m_mode != LOW_LATENCY_VRR)
        return 0;

      // Presentation latency should be fairly stable, but drivers may report back
      // different levels of latency (Nvidia reports very low latencies on x11 flip compared
      // to Wayland). We take the median within a recent time window to adjust to that.

      // Presentation latency may vary though for other reasons, like when compiling shaders
      // on all cpu cores, we will get thread starvation and higher latency.

      int32_t presentLatency = m_presentationStats.getMedian( frameId );
      uint64_t frameFinishedId = m_latencyMarkersStorage->getTimeline()->frameFinished.load();
      int32_t lastVBlank = std::chrono::duration_cast<microseconds> (
        m_latencyMarkersStorage->getConstMarkers(frameFinishedId)->end - now).count()
        - presentLatency;
      int32_t targetVBlank = lastVBlank + (frameId-frameFinishedId) * m_vrrRefreshInterval;

      // set last v-blank if we have more information about the last frame
      if (frameFinishedId != frameId-1 && lastFrameFinish != time_point{} ) {
        assert( frameFinishedId == frameId-2 );
        int32_t vBlank = std::chrono::duration_cast<microseconds> (lastFrameFinish - now).count();
        lastVBlank += m_vrrRefreshInterval;
        lastVBlank = std::max( lastVBlank, vBlank );
        targetVBlank = lastVBlank + m_vrrRefreshInterval;
      }

      int32_t expectedFrameLatency = props.cpuUntilGpuStart + props.optimizedGpuTime;
      return targetVBlank - expectedFrameLatency;

    }


    void sleepFor( const Sleep::TimePoint t, int32_t delay ) {

      if (delay <= 0)
        return;

      int32_t maxDelay = std::max( m_fpsLimitFrametime.load(), 20000 );
      delay = std::min( delay, maxDelay );

      Sleep::TimePoint t2 = t + microseconds(delay);
      Sleep::sleepUntil( t, t2 );

    }


    int32_t getLowLatencyOffset( const DxvkOptions& options );
    bool getLowLatencyAllowCpuFramesOverlap( const DxvkOptions& options );

    const int32_t m_lowLatencyOffset;
    const bool    m_allowCpuFramesOverlap;

    int32_t m_vrrRefreshInterval = { 0 };
    LatencyStats m_presentationStats;

    std::array<SyncProps, 16> m_props = { };
    std::atomic<uint64_t> m_propsFinished = { 0 };

    std::vector<int32_t>  m_tempGpuRun;

    GpuProgress m_gpuProgress;

  };

}
