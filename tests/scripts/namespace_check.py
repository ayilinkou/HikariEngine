#!/usr/bin/env python3
"""Enforce directory-derived public namespaces and prohibit using-directives in headers."""

from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from hikari_tools.common import ROOT, Parser, ToolError, entrypoint
from hikari_tools.source import code_lines, source_files


# Headers declaring nothing a namespace could hold. Stale entries fail too.
NO_NAMESPACE_ALLOWLIST = {"engine/core/include/core/MyMacros.h"}
NAMESPACE_SPELLINGS = {"d3d12": "D3D12"}


def check(root=ROOT):
    errors = []
    seen = set()
    checked = 0
    for module in sorted((root / "engine").glob("*")):
        include = module / "include"
        for header in sorted(include.rglob("*.h")):
            relative = header.relative_to(root).as_posix()
            components = header.relative_to(include).parent.parts
            expected = "::".join(["Hikari", *[
                NAMESPACE_SPELLINGS.get(component, component[:1].upper() + component[1:])
                for component in components
            ]])
            pattern = re.compile(r"^namespace[ \t]+" + re.escape(expected)
                                 + r"(::[A-Za-z_][A-Za-z0-9_]*)*[ \t]*(\{)?[ \t]*$")
            found = any(pattern.search(code) for _, _, code in code_lines(header))
            if relative in NO_NAMESPACE_ALLOWLIST:
                seen.add(relative)
                if found:
                    errors.append(f"  {relative}: opens {expected}; remove the no-namespace allowlist entry")
            else:
                checked += 1
                if not found:
                    errors.append(f"  {relative}: expected namespace {expected}")
    for relative in sorted(NO_NAMESPACE_ALLOWLIST - seen):
        errors.append(f"  {relative}: allowlisted header was not scanned; remove the entry")

    headers = source_files(root, ("engine", "src", "tests"), extensions=(".h",))
    for header in headers:
        for number, _, code in code_lines(header):
            if re.search(r"(^|[^A-Za-z0-9_])using[ \t]+namespace[ \t]", code):
                errors.append(f"  {header.relative_to(root).as_posix()}:{number}: using-directive in a header: {code}")
    if errors:
        raise ToolError("namespace_check failed:\n" + "\n".join(errors))
    return [f"namespace_check: {checked} public engine header(s) open their module's namespace; "
            f"{len(headers)} header(s) free of using-directives."]


def main(argv=None):
    Parser(description=__doc__).parse_args(argv)
    print("\n".join(check()))
    return 0


if __name__ == "__main__":
    raise SystemExit(entrypoint(main))
