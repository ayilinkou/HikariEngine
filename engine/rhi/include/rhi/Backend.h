#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace Hikari::Rhi
{

/**
 * Which implementation of this API a device is built from.
 *
 * Here rather than in RhiTypes.h, which documents an invariant this enum cannot
 * keep: every enum there is paired with a conversion table in the backend whose
 * switch carries no `default:`, so the build breaks until the mapping exists.
 * This one selects *which* backend runs rather than naming vocabulary a backend
 * translates, so there is nothing for it to map to.
 */
enum class Backend : uint8_t
{
    Vulkan,
    D3D12,
};

/**
 * The backends this build contains. Vulkan is always among them (plan D25).
 *
 * Answers what was compiled in, not what this machine can run: no instance is
 * created and no adapter enumerated, so it is a fact rather than a measurement
 * and it costs nothing to ask. A backend that is present but cannot create a
 * device here — a missing loader, an adapter below the required feature level —
 * fails in CreateDevice, with whatever the backend can say about why. One word
 * covering both would print "available: vulkan" on a machine where Vulkan is
 * installed and its loader is broken.
 *
 * Membership is the availability test; there is no second predicate, because
 * every caller wants either this list or membership in it.
 */
[[nodiscard]] std::span<const Backend> AvailableBackends();

/**
 * The backend's name, and the only spelling of it.
 *
 * The RHI owns the spelling of the enums that cross the process boundary —
 * those appearing as command-line input or run-report output — so `--backend
 * D3D12` and a report's `"backend": "D3D12"` cannot drift apart. A comparison
 * of two runs matches that field as text and has to accept the word the other
 * side wrote.
 *
 * Proper nouns, as the project spells them everywhere else, which also keeps a
 * run report internally consistent: its `os` is "Linux" and its `arch` is
 * "x86_64", because one is a name and the other an identifier.
 */
[[nodiscard]] std::string_view ToString(Backend backend);

/**
 * The inverse, returning nothing for a word that names no backend.
 *
 * Case-insensitive, because this half is typed by hand on a command line while
 * ToString's half is written into a file. Every spelling of the word reaches the
 * same backend, and only one of them is ever written back out.
 */
[[nodiscard]] std::optional<Backend> BackendFromString(std::string_view name);

} // namespace Hikari::Rhi
