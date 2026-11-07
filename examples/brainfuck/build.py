#!/usr/bin/env python3

import os
import shlex
import subprocess
from pathlib import Path


def environment_command(primary: str, fallback: str) -> list[str]:
    value = os.environ.get(primary) or fallback
    return shlex.split(value)


def run(command: list[str]) -> None:
    subprocess.run(command, check=True)


def main() -> None:
    case_dir = Path(__file__).resolve().parent
    repo_root = case_dir.parents[1]
    build_dir = repo_root / "build" / "brainfuck"

    ccompiler = environment_command(
        "CCOMPILER", str(repo_root / "build" / "ccompiler")
    )
    host_cc = environment_command("HOST_CC", os.environ.get("CC") or "clang")

    build_dir.mkdir(parents=True, exist_ok=True)

    main_ll = build_dir / "main.ll"
    main_bc = build_dir / "main.bc"
    main_object = build_dir / "main.o"
    host_object = build_dir / "host.o"
    executable = build_dir / "brainfuck"

    run([*ccompiler, str(case_dir / "main.c"), "-o", str(main_ll)])
    run(["llvm-as", str(main_ll), "-o", str(main_bc)])
    run(
        [
            "llc",
            str(main_bc),
            "-relocation-model=pic",
            "-filetype=obj",
            "-o",
            str(main_object),
        ]
    )
    run(
        [
            *host_cc,
            "-std=c11",
            "-O2",
            "-Wall",
            "-Wextra",
            "-c",
            str(case_dir / "host.c"),
            "-o",
            str(host_object),
        ]
    )
    run([*host_cc, str(main_object), str(host_object), "-o", str(executable)])


if __name__ == "__main__":
    main()
