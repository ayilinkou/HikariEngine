"""Source scanning shared by the namespace and RHI boundary checks."""


def code_lines(path):
    """Yield (number, original, code), preserving line numbers and old rules.

    Strip // and /* */ comments, including blocks spanning lines. String literals
    are deliberately not special-cased: backend names in strings count too.
    """
    in_block = False
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        rest = line
        result = ""
        while rest:
            if in_block:
                close = rest.find("*/")
                if close == -1:
                    break
                rest = rest[close + 2:]
                in_block = False
            else:
                single = rest.find("//")
                block = rest.find("/*")
                if block != -1 and (single == -1 or block < single):
                    result += rest[:block]
                    rest = rest[block + 2:]
                    in_block = True
                elif single != -1:
                    result += rest[:single]
                    break
                else:
                    result += rest
                    break
        yield number, line, result


def source_files(root, directories, extensions=(".h", ".cpp")):
    return sorted(path for directory in directories for path in (root / directory).rglob("*")
                  if path.is_file() and path.suffix in extensions)
