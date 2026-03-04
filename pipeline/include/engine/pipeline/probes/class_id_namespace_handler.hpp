#pragma once

#include "engine/core/config/config_types.hpp"

#include <gst/gst.h>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace engine::pipeline::probes {

/**
 * @brief Pad probe that namespaces class IDs across all GIEs automatically.
 *
 * Two modes of operation:
 *   - **Offset**: For every object in the batch, computes
 *     `offset = (unique_component_id - base_no_offset_) * offset_step_` and
 *     applies it if offset > 0.  GIEs whose `unique_component_id <= base_no_offset_`
 *     (default: PGIE with id=1) are left untouched — class IDs stay at 0..N.
 *     Original values are stored in `misc_obj_info[]` with a magic marker to
 *     prevent double-processing (idempotent).
 *   - **Restore**: Reads back the stored originals from `misc_obj_info[]` and
 *     restores `class_id` and `unique_component_id` to pre-offset values.
 *
 * Attach the **Offset** probe on the **sink** pad of nvtracker (runs before tracker).
 * Attach the **Restore** probe on the **src** pad of nvtracker (runs after tracker).
 *
 * Formula (matches lantanav2 behavior):
 *   GIE 1 (PGIE)  → offset = 0     (class_ids 0..N unchanged)
 *   GIE 2 (SGIE)  → offset = 1000  (class_ids 1000..)
 *   GIE 3 (SGIE)  → offset = 2000  (class_ids 2000..)
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
     * @brief Configure the probe mode.
     *
     * In Offset mode, ALL GIEs whose unique_component_id > base_no_offset_ will
     * be automatically remapped using offset = (uid - base_no_offset_) * offset_step_.
     * No per-element index is required.
     *
     * @param config  Full pipeline config (unused in Offset, kept for consistency).
     * @param mode    Offset or Restore.
     * @param base_no_offset  GIEs with uid <= this value are NOT offset (default 1 = PGIE).
     * @param offset_step     Multiplier per GIE level (default 1000).
     */
    void configure(const engine::core::config::PipelineConfig& config, Mode mode,
                   int base_no_offset = 1, int offset_step = 1000);

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
    int base_no_offset_ = 1;  ///< GIEs with uid <= this are NOT offset (PGIE = 1 by default)
    int offset_step_ = 1000;  ///< Multiplier: offset = (uid - base_no_offset_) * offset_step_

    /// Optional explicit overrides: gie_unique_id -> exact offset (overrides formula)
    std::unordered_map<int, int> explicit_offsets_;

    /// Magic marker stored in misc_obj_info[0] to flag processed objects
    static constexpr int64_t MAGIC_MARKER = 0x4C4E5441;  // "LNTA"

    /**
     * @brief Compute offset for a given GIE unique_component_id.
     * @return 0 if uid <= base_no_offset_, else (uid - base_no_offset_) * offset_step_.
     */
    int compute_offset(int gie_unique_id) const;

    /** @brief Offset mode callback logic. */
    GstPadProbeReturn process_offset(GstBuffer* buf);

    /** @brief Restore mode callback logic. */
    GstPadProbeReturn process_restore(GstBuffer* buf);
};

}  // namespace engine::pipeline::probes
