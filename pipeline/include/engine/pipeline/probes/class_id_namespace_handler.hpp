#pragma once

#include "engine/core/config/config_types.hpp"

#include <gst/gst.h>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace engine::pipeline::probes {

/**
 * @brief Pad probe that namespaces class IDs based on GIE unique-id.
 *
 * Two modes of operation:
 *   - **Offset**: Remaps `class_id` to `base_offset + original_class_id`.
 *     The original values are stored in `misc_obj_info[]` with a magic marker
 *     to prevent double-processing (idempotent).
 *   - **Restore**: Reads back the stored originals from `misc_obj_info[]` and
 *     restores `class_id` and `unique_component_id` to pre-offset values.
 *
 * Attach Offset probe on the src pad of each nvinfer element.
 * Attach Restore probe before OSD or any element that needs original IDs.
 *
 * Magic marker in misc_obj_info[0]: 0x4C4E5441 ("LNTA") indicates offset was applied.
 *   misc_obj_info[1] = original class_id
 *   misc_obj_info[2] = original unique_component_id
 */
class ClassIdNamespaceHandler {
   public:
    /** @brief Operating mode for the namespace handler. */
    enum class Mode { Offset, Restore };

    ClassIdNamespaceHandler() = default;
    ~ClassIdNamespaceHandler() = default;

    /**
     * @brief Configure for a specific processing element.
     *
     * In Offset mode, reads the element's unique_id to compute the offset.
     * In Restore mode, no element-specific config is needed.
     *
     * @param config  Full pipeline config.
     * @param mode    Offset or Restore.
     * @param element_index  Index into config.processing.elements (Offset only).
     */
    void configure(const engine::core::config::PipelineConfig& config, Mode mode,
                   int element_index = -1);

    /**
     * @brief Set explicit offset map for specific GIE IDs.
     *
     * If a GIE unique_id is found in this map, the mapped offset is used
     * instead of the formula (unique_id * offset_step_).
     *
     * @param offsets  Map of gie_unique_id -> desired offset.
     */
    void set_explicit_offsets(const std::unordered_map<int, int>& offsets);

    /** @brief Static probe callback. user_data -> ClassIdNamespaceHandler*. */
    static GstPadProbeReturn on_buffer(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

   private:
    Mode mode_ = Mode::Offset;
    int gie_unique_id_ = 0;   ///< For Offset mode: which GIE to apply offset to
    int offset_step_ = 1000;  ///< Multiplier for formula: gie_id * step
    int base_offset_ = 0;     ///< Precomputed offset for this specific GIE

    /// Optional explicit offsets: gie_unique_id -> offset
    std::unordered_map<int, int> explicit_offsets_;

    /// Magic marker stored in misc_obj_info[0] to flag processed objects
    static constexpr int64_t MAGIC_MARKER = 0x4C4E5441;  // "LNTA"

    /** @brief Compute offset for a given GIE unique_id. */
    int compute_offset(int gie_unique_id) const;

    /** @brief Offset mode callback logic. */
    GstPadProbeReturn process_offset(GstBuffer* buf);

    /** @brief Restore mode callback logic. */
    GstPadProbeReturn process_restore(GstBuffer* buf);
};

}  // namespace engine::pipeline::probes
