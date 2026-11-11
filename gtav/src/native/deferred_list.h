// gtav - recording a frame on two threads (--gtav_native_record_thread).
//
// The backend's work per draw is mostly its own (textures, vertex conversion, constants, pipeline
// lookups), but about a fifth of the render thread went to plume and the Vulkan driver turning
// the calls into commands. DeferredCommandList takes those calls as a compact stream and a
// second thread replays them into the real command list while the backend goes on with the next
// draws. Everything the calls point at (rects, barriers, views, push constants) is copied into
// the stream; the objects themselves already live until the frame's fence.

#pragma once

#include <plume_render_interface.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace gtav::native {

class DeferredCommandList final : public plume::RenderCommandList {
 public:
  // on_start runs first on the replay thread (core pinning).
  explicit DeferredCommandList(std::function<void()> on_start = {});
  ~DeferredCommandList() override;

  // Calls from now on go to `target` (begun by the caller) through the replay thread.
  void Begin(plume::RenderCommandList* target);
  // Waits until every call so far is in `target`. Returns the ms the caller waited.
  double Finish();
  // ms the replay thread spent replaying since the last call.
  double TakeBusyMs();

  void begin() override {}
  void end() override {}
  void barriers(plume::RenderBarrierStages stages, const plume::RenderBufferBarrier* bufferBarriers,
                uint32_t bufferBarriersCount, const plume::RenderTextureBarrier* textureBarriers,
                uint32_t textureBarriersCount) override;
  void dispatch(uint32_t x, uint32_t y, uint32_t z) override;
  void traceRays(uint32_t, uint32_t, uint32_t, plume::RenderBufferReference,
                 const plume::RenderShaderBindingGroupsInfo&) override;
  void drawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertexLocation,
                     uint32_t startInstanceLocation) override;
  void drawIndexedInstanced(uint32_t indexCountPerInstance, uint32_t instanceCount, uint32_t startIndexLocation,
                            int32_t baseVertexLocation, uint32_t startInstanceLocation) override;
  void setPipeline(const plume::RenderPipeline* pipeline) override;
  void setComputePipelineLayout(const plume::RenderPipelineLayout* pipelineLayout) override;
  void setComputePushConstants(uint32_t rangeIndex, const void* data, uint32_t offset, uint32_t size) override;
  void setComputeDescriptorSet(plume::RenderDescriptorSet* descriptorSet, uint32_t setIndex) override;
  void setGraphicsPipelineLayout(const plume::RenderPipelineLayout* pipelineLayout) override;
  void setGraphicsPushConstants(uint32_t rangeIndex, const void* data, uint32_t offset, uint32_t size) override;
  void setGraphicsDescriptorSet(plume::RenderDescriptorSet* descriptorSet, uint32_t setIndex) override;
  void setGraphicsDescriptorSetDynamic(plume::RenderDescriptorSet* descriptorSet, uint32_t setIndex,
                                       const uint32_t* dynamicOffsets, uint32_t dynamicOffsetCount) override;
  void setGraphicsRootDescriptor(plume::RenderBufferReference bufferReference, uint32_t rootDescriptorIndex) override;
  void setRaytracingPipelineLayout(const plume::RenderPipelineLayout*) override;
  void setRaytracingPushConstants(uint32_t, const void*, uint32_t, uint32_t) override;
  void setRaytracingDescriptorSet(plume::RenderDescriptorSet*, uint32_t) override;
  void setIndexBuffer(const plume::RenderIndexBufferView* view) override;
  void setVertexBuffers(uint32_t startSlot, const plume::RenderVertexBufferView* views, uint32_t viewCount,
                        const plume::RenderInputSlot* inputSlots) override;
  void setViewports(const plume::RenderViewport* viewports, uint32_t count) override;
  void setScissors(const plume::RenderRect* scissorRects, uint32_t count) override;
  void setFramebuffer(const plume::RenderFramebuffer* framebuffer) override;
  void setDepthBias(float depthBias, float depthBiasClamp, float slopeScaledDepthBias) override;
  void clearColor(uint32_t attachmentIndex, plume::RenderColor colorValue, const plume::RenderRect* clearRects,
                  uint32_t clearRectsCount) override;
  void clearDepthStencil(bool clearDepth, bool clearStencil, float depthValue, uint32_t stencilValue,
                         const plume::RenderRect* clearRects, uint32_t clearRectsCount) override;
  void copyBufferRegion(plume::RenderBufferReference dstBuffer, plume::RenderBufferReference srcBuffer,
                        uint64_t size) override;
  void copyTextureRegion(const plume::RenderTextureCopyLocation& dstLocation,
                         const plume::RenderTextureCopyLocation& srcLocation, uint32_t dstX, uint32_t dstY,
                         uint32_t dstZ, const plume::RenderBox* srcBox) override;
  void copyBuffer(const plume::RenderBuffer* dstBuffer, const plume::RenderBuffer* srcBuffer) override;
  void copyTexture(const plume::RenderTexture* dstTexture, const plume::RenderTexture* srcTexture) override;
  void resolveTexture(const plume::RenderTexture* dstTexture, const plume::RenderTexture* srcTexture) override;
  void resolveTextureRegion(const plume::RenderTexture* dstTexture, uint32_t dstX, uint32_t dstY,
                            const plume::RenderTexture* srcTexture, const plume::RenderRect* srcRect,
                            plume::RenderResolveMode resolveMode) override;
  void buildBottomLevelAS(const plume::RenderAccelerationStructure*, plume::RenderBufferReference,
                          const plume::RenderBottomLevelASBuildInfo&) override;
  void buildTopLevelAS(const plume::RenderAccelerationStructure*, plume::RenderBufferReference,
                       plume::RenderBufferReference, const plume::RenderTopLevelASBuildInfo&) override;
  void discardTexture(const plume::RenderTexture* texture) override;
  void resetQueryPool(const plume::RenderQueryPool* queryPool, uint32_t queryFirstIndex, uint32_t queryCount) override;
  void writeTimestamp(const plume::RenderQueryPool* queryPool, uint32_t queryIndex) override;
  void beginOcclusionQuery(const plume::RenderQueryPool* queryPool, uint32_t queryIndex) override;
  void endOcclusionQuery(const plume::RenderQueryPool* queryPool, uint32_t queryIndex) override;
  void setFragmentShadingRate(uint32_t width, uint32_t height) override;

 private:
  enum class Op : uint16_t;
  struct Header {
    Op op;
    uint16_t pad;
    uint32_t size;  // of the whole record, header included, a multiple of 8
  };
  static constexpr uint32_t kBlockSize = 1u << 20;
  static constexpr uint32_t kBlocks = 32;  // then the caller waits for the replay and starts over
  struct Block {
    std::unique_ptr<uint8_t[]> data;
    std::atomic<uint32_t> used{0};  // published bytes (release)
  };

  // A record of `payload` bytes; Publish() after filling it.
  uint8_t* Append(Op op, uint32_t payload);
  void Publish();
  template <class T>
  static uint32_t Bytes(uint32_t count) {
    return uint32_t(sizeof(T)) * count;
  }
  void Wake();
  void Run();
  void Replay(const Header* h);

  plume::RenderCommandList* target_ = nullptr;
  std::array<Block, kBlocks> blocks_;
  // Producer side.
  uint32_t write_block_ = 0, write_offset_ = 0;
  uint64_t appended_ = 0;  // records
  uint64_t last_wake_ = 0;
  // Replay side.
  uint32_t read_block_ = 0, read_offset_ = 0;
  std::atomic<uint64_t> replayed_{0};
  std::atomic<double> busy_ms_{0};

  std::function<void()> on_start_;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable work_cv_, done_cv_;
  std::atomic<bool> sleeping_{false};
  std::atomic<bool> waiting_{false};  // the producer waits in Finish
  std::atomic<uint64_t> published_{0};  // records
  bool stop_ = false;
};

}  // namespace gtav::native
