"""
One function of the Shadow decompile without reading it whole (shadowDecomp/decompiled_nops/pseudocode.c, ~850k lines).

    python3 tools/decompFunc.py FUN_800221a8                  summary: line range in pseudocode.c, line count, called functions, DAT_ refs
    python3 tools/decompFunc.py FUN_800221a8 --grep PATTERN [-C N]   lines matching the regex, N lines of context (default 2), file line numbers
    python3 tools/decompFunc.py FUN_800221a8 --lines A-B      lines A..B of the function (1 = its first line)
    python3 tools/decompFunc.py 800221a8 --file other.c       name or address; another pseudocode file

The file starts each function with a line `// FUN <address> <name> <size>` (written by tools/decomp/shadow); the function ends at the next one.
The summary's line range can be read directly with the Read tool's offset/limit.
"""
import re
import sys

DEFAULT = "shadowDecomp/decompiled_nops/pseudocode.c"


def main():
    args = sys.argv[1:]
    options = {"--grep": None, "-C": "2", "--lines": None, "--file": DEFAULT}
    for flag in list(options):
        if flag in args:
            i = args.index(flag)
            options[flag] = args[i + 1]
            del args[i:i + 2]
    if len(args) != 1:
        sys.exit(__doc__)
    wanted = args[0].lower().removeprefix("fun_")
    start = None
    body = []
    with open(options["--file"]) as f:
        for number, line in enumerate(f, 1):
            if line.startswith("// FUN "):
                if start is not None:
                    break
                fields = line.split()
                if fields[2].lower() == wanted or fields[3].lower() == args[0].lower():
                    start = number
                    print(f"# {fields[3]} at {fields[2]}, {fields[4]} bytes")
            if start is not None:
                body.append(line.rstrip("\n"))
    if start is None:
        sys.exit("not found: " + args[0])
    if options["--grep"]:
        pattern = re.compile(options["--grep"])
        context = int(options["-C"])
        shown = set()
        for i, line in enumerate(body):
            if pattern.search(line):
                shown.update(range(max(0, i - context), min(len(body), i + context + 1)))
        last = None
        for i in sorted(shown):
            if last is not None and i != last + 1:
                print("--")
            print(f"{start + i}: {body[i]}")
            last = i
    elif options["--lines"]:
        a, b = (int(v) for v in options["--lines"].split("-"))
        for i in range(a - 1, min(b, len(body))):
            print(f"{start + i}: {body[i]}")
    else:
        text = "\n".join(body)
        calls = sorted(set(re.findall(r"\b(FUN_[0-9a-f]{8})\s*\(", text)) - {body[0].split()[3]})
        data = sorted(set(re.findall(r"\bDAT_[0-9a-f]{8}\b", text)))
        print(f"lines {start}-{start + len(body) - 1} ({len(body)})")
        print("calls " + (" ".join(calls) or "-"))
        print("data " + (" ".join(data) or "-"))


main()
