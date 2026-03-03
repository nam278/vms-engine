#pragma once
#include "engine/core/builders/ibuilder_factory.hpp"
#include <gst/gst.h>
#include <string>
#include <unordered_map>
#include <functional>

namespace engine::pipeline {

/**
 * @brief Concrete IBuilderFactory — maps GStreamer type strings to builders.
 *
 * Each builder receives the pipeline bin in its constructor. The factory
 * stores a creation-function map and lazily creates builders on demand.
 */
class BuilderFactory : public engine::core::builders::IBuilderFactory {
   public:
    /**
     * @param bin Pipeline bin passed to each created builder.
     */
    explicit BuilderFactory(GstElement* bin);

    /**
     * @brief Create an element builder for the given GStreamer type.
     * @param type GStreamer factory name (e.g. "nvinfer", "nvtracker").
     * @return Unique pointer to builder, or nullptr if type unknown.
     */
    std::unique_ptr<engine::core::builders::IElementBuilder> create(
        const std::string& type) override;

   private:
    GstElement* bin_ = nullptr;

    using CreatorFn =
        std::function<std::unique_ptr<engine::core::builders::IElementBuilder>(GstElement*)>;
    std::unordered_map<std::string, CreatorFn> creators_;

    void register_defaults();
};

}  // namespace engine::pipeline
