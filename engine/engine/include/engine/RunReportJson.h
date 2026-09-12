#pragma once

#include <string>

#include <engine/RunReport.h>

namespace Hikari::Engine
{

/**
 * The run report as JSON — exactly the bytes that land in the file.
 *
 * Separate from writing it so that the format is reachable without a
 * filesystem. The comparison tool's table classifies every field this emits,
 * and the test enforcing that has to ask this function what it emits rather
 * than carry a second list of field names, which would agree with the format
 * only until the next field is added.
 */
[[nodiscard]] std::string ToJson(const RunReport& report);

} // namespace Hikari::Engine
