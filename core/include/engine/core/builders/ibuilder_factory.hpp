#pragma once
#include "engine/core/builders/ielement_builder.hpp"
#include <memory>
#include <string>

namespace engine::core::builders {

/**
 * @brief Factory that creates element builders by GStreamer type name.
 */
class IBuilderFactory {
   public:
    virtual ~IBuilderFactory() = default;
    virtual std::unique_ptr<IElementBuilder> create(const std::string& type) = 0;
};

}  // namespace engine::core::builders
