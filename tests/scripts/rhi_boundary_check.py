#!/usr/bin/env python3
"""Guard the RHI's neutral seam, transitional escape hatches and backend isolation.

These textual checks complement HeaderSelfContainment: a backend header on a
system include path can compile without its target dependency and still violate
the boundary. Keep the five checks and their shrinking allowlists together.
"""

from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from hikari_tools.common import ROOT, Parser, ToolError, entrypoint
from hikari_tools.source import code_lines, source_files


VULKAN_PATTERNS = (
    r"vk::", r"(^|[^A-Za-z0-9_])Vk[A-Z]", r"(^|[^A-Za-z0-9_])Vma[A-Z]",
    r"(^|[^A-Za-z0-9_])VMA_", r'#[ \t]*include[ \t]*[<"]vulkan/',
    r'#[ \t]*include[ \t]*[<"]vk_mem_alloc',
)
# Bare D3D12 is neutral vocabulary (Backend::D3D12), as Backend::Vulkan is.
D3D12_PATTERNS = (
    r"(^|[^A-Za-z0-9_])ID3D12[A-Za-z]", r"(^|[^A-Za-z0-9_])IDXGI[A-Za-z]",
    r"(^|[^A-Za-z0-9_])D3D12_", r"(^|[^A-Za-z0-9_])DXGI_",
    r"(^|[^A-Za-z0-9_])D3D12MA", r'#[ \t]*include[ \t]*[<"](directx/|d3d12|dxgi)',
)
BANNED_PATTERNS = VULKAN_PATTERNS + D3D12_PATTERNS

# Frozen public escape hatches. ImGui requires raw API objects (D9/D45).
# SwapchainUtil deliberately stays public for device-free tests of surface
# states that a real display cannot be asked for, including zero extent.
TRANSITIONAL_HEADERS = {
    "vulkan/VulkanNative.h", "vulkan/SwapchainUtil.h", "d3d12/D3D12Native.h",
}
# (path, header) -> why it still needs access. Removing a use requires removing
# its entry too, so an exception cannot silently outlive the code it excuses.
TRANSITIONAL_ALLOWLIST = {
    ("engine/editor/src/VulkanUiBackend.cpp", "vulkan/VulkanNative.h"):
        "ImGui takes instance/device/queue and a VkCommandBuffer by value (D9)",
    ("engine/editor/src/D3D12UiBackend.cpp", "d3d12/D3D12Native.h"):
        "ImGui takes device/queue/command list/descriptor heap by value (D45)",
    ("tests/unit/rhi/SwapchainUtilTests.cpp", "vulkan/SwapchainUtil.h"):
        "Surface states a real display cannot be put into on demand",
    ("tests/gpu/rhi/DeviceTests.cpp", "vulkan/VulkanNative.h"):
        "The escape hatch is what these cases assert on",
}
NAMING_ALLOWLISTS = {
    "vulkan": {"engine/editor/src/VulkanUiBackend.cpp":
               "ImGui's Vulkan backend requires raw API objects (D9). Permanent; D3D12 has a sibling file"},
    "d3d12": {"engine/editor/src/D3D12UiBackend.cpp":
              "ImGui's DX12 backend requires raw API objects and DXGI formats (D45). Permanent"},
}
# These factory headers expose no backend API. Exempt just these exact lines,
# not the whole dispatcher; otherwise it could acquire an API dependency.
SHARED_EXEMPTIONS = {
    ('engine/rhi/src/Backend.cpp', '#include "vulkan/VulkanDeviceFactory.h"'),
    ('engine/rhi/src/Backend.cpp', '#include "d3d12/D3D12DeviceFactory.h"'),
}
TRANSITIONAL_INCLUDE = re.compile(
    r'^[ \t]*#[ \t]*include[ \t]*[<"]rhi/((vulkan|d3d12)/[A-Za-z0-9_]+\.h)[>"]'
)


def fail(title, findings):
    raise ToolError("rhi_boundary_check: " + title + "\n" + "\n".join(findings))


def scan(path, root, patterns, exemptions=()):
    relative = path.relative_to(root).as_posix()
    findings = []
    used = set()
    for number, original, code in code_lines(path):
        exemption = (relative, original.strip())
        if exemption in exemptions:
            used.add(exemption)
        elif any(re.search(pattern, code) for pattern in patterns):
            findings.append(f"  {relative}:{number}: {code}")
    return findings, used


def check(root=ROOT):
    messages = []
    neutral = root / "engine/rhi/include/rhi"
    headers = sorted(neutral.glob("*.h"))
    if not headers:
        fail("no neutral RHI headers found.", [str(neutral)])

    # 1. Non-recursive: the transitional subdirectories belong to check 2.
    violations = [finding for header in headers for finding in scan(header, root, BANNED_PATTERNS)[0]]
    if violations:
        fail("neutral RHI headers must not depend on a backend's API. "
             "Move backend-facing declarations into engine/rhi/src/<backend>/.", violations)
    messages.append(f"rhi_boundary_check: {len(headers)} neutral RHI header(s) free of Vulkan, VMA, D3D12 and D3D12MA.")

    # 2. Both new headers and stale entries fail.
    present = {path.relative_to(neutral).as_posix() for backend in ("vulkan", "d3d12")
               for path in (neutral / backend).glob("*.h")}
    unexpected = present - TRANSITIONAL_HEADERS
    if unexpected:
        fail("unexpected header in an area that may expose a backend. "
             "Prefer private headers; justify exceptions in TRANSITIONAL_HEADERS.", sorted(unexpected))
    missing = TRANSITIONAL_HEADERS - present
    if missing:
        fail("TRANSITIONAL_HEADERS lists headers that no longer exist; remove the entries.", sorted(missing))

    # 3. Includes outside RHI are constrained independently of API names.
    scanned = source_files(root, ("apps", "tests", "engine"))
    outside = [path for path in scanned if not path.relative_to(root).as_posix().startswith("engine/rhi/")]
    matched = set()
    unlisted = []
    for path in outside:
        relative = path.relative_to(root).as_posix()
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            # Preserve the old anchored include check; // includes do not count.
            match = TRANSITIONAL_INCLUDE.search(line)
            if match:
                key = (relative, match[1])
                if key in TRANSITIONAL_ALLOWLIST:
                    matched.add(key)
                else:
                    unlisted.append(f"  {relative}:{number}: rhi/{match[1]}")
    if unlisted:
        fail("new use of an RHI area that exposes a backend. Prefer the neutral API; "
             "justify exceptions in TRANSITIONAL_ALLOWLIST.", unlisted)
    stale = set(TRANSITIONAL_ALLOWLIST) - matched
    if stale:
        fail("TRANSITIONAL_ALLOWLIST has entries nothing matches; remove them.",
             [f"  {path} -> {header}: {TRANSITIONAL_ALLOWLIST[path, header]}" for path, header in sorted(stale)])
    messages.append(f"rhi_boundary_check: transitional area is {len(present)} header(s), "
                    f"used from {len(matched)} site(s) outside the module.")

    # 4. API names in engine/apps cannot bypass the include rule through a PCH.
    # Tests intentionally exercise backend conversions and remain exempt here.
    naming_files = [path for path in outside if path.relative_to(root).parts[0] != "tests"]
    for backend, patterns in (("vulkan", VULKAN_PATTERNS), ("d3d12", D3D12_PATTERNS)):
        allowed = NAMING_ALLOWLISTS[backend]
        seen = set()
        violations = []
        for path in naming_files:
            relative = path.relative_to(root).as_posix()
            if relative in allowed:
                seen.add(relative)
            else:
                violations.extend(scan(path, root, patterns)[0])
        if violations:
            fail(f"{backend} named outside engine/rhi/. Use the neutral API; "
                 f"justify exceptions in NAMING_ALLOWLISTS[{backend!r}].", violations)
        stale = set(allowed) - seen
        if stale:
            fail(f"{backend} naming allowlist has entries nothing matches; remove them.", sorted(stale))
        messages.append(f"rhi_boundary_check: {len(allowed)} file(s) outside engine/rhi/ may name {backend}.")

    # 5. Backend sources name only their own API; shared sources name neither.
    rhi = root / "engine/rhi"
    sides = {}
    for backend in ("vulkan", "d3d12"):
        sides[backend] = sorted(path for directory in (rhi / "src" / backend, neutral / backend)
                                for path in directory.rglob("*") if path.is_file())
    shared = sorted(path for path in (rhi / "src").glob("*") if path.is_file() and path.suffix in (".h", ".cpp"))
    violations = []
    used = set()
    for paths, patterns, exemptions in (
        (sides["vulkan"], D3D12_PATTERNS, ()),
        (sides["d3d12"], VULKAN_PATTERNS, ()),
        (shared, BANNED_PATTERNS, SHARED_EXEMPTIONS),
    ):
        for path in paths:
            findings, matched = scan(path, root, patterns, exemptions)
            violations.extend(findings)
            used.update(matched)
    if violations:
        fail("a backend named where it does not belong inside engine/rhi/. "
             "Shared helpers must be neutral or duplicated within each backend.", violations)
    if SHARED_EXEMPTIONS - used:
        fail("SHARED_EXEMPTIONS has entries nothing matches; remove them.",
             [f"  {path} -> {line}" for path, line in sorted(SHARED_EXEMPTIONS - used)])
    messages.append(f"rhi_boundary_check: {len(sides['vulkan'])} Vulkan, {len(sides['d3d12'])} D3D12 "
                    f"and {len(shared)} shared file(s) inside engine/rhi/ keep to their own API.")
    return messages


def main(argv=None):
    Parser(description=__doc__).parse_args(argv)
    print("\n".join(check()))
    return 0


if __name__ == "__main__":
    raise SystemExit(entrypoint(main))
