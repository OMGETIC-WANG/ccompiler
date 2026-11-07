#!/usr/bin/env python3

import subprocess
import sys
import tempfile
from pathlib import Path


class TestFailure(Exception):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise TestFailure(message)


def run_program(
    executable: Path,
    *arguments: Path | str,
    input_data: bytes = b"",
    timeout: float | None = None,
) -> subprocess.CompletedProcess[bytes]:
    command = [str(executable), *(str(argument) for argument in arguments)]
    try:
        return subprocess.run(
            command,
            input=input_data,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise TestFailure(
            f"{Path(command[0]).name}: timed out after {timeout}s"
        ) from error


def require_error(
    result: subprocess.CompletedProcess[bytes], expected_message: str, test_name: str
) -> None:
    require(
        result.returncode != 0,
        f"{test_name}: expected a nonzero status, got {result.returncode}",
    )
    require(
        expected_message.encode() in result.stderr,
        f"{test_name}: expected {expected_message!r} on stderr",
    )


def main() -> None:
    case_dir = Path(__file__).resolve().parent
    repo_root = case_dir.parents[1]
    executable = repo_root / "build" / "brainfuck" / "brainfuck"
    tests_dir = case_dir / "tests"

    subprocess.run([sys.executable, str(case_dir / "build.py")], check=True)

    with tempfile.TemporaryDirectory(prefix="brainfuck-test-") as temporary_dir:
        temporary_path = Path(temporary_dir)

        hello = run_program(executable, tests_dir / "hello.bf")
        require(hello.returncode == 0, f"hello test: exit status {hello.returncode}")
        require(
            hello.stdout == (tests_dir / "hello.expected").read_bytes(),
            "hello test: unexpected output",
        )

        echo = run_program(
            executable,
            tests_dir / "echo.bf",
            input_data=(tests_dir / "echo.in").read_bytes(),
        )
        require(echo.returncode == 0, f"echo test: exit status {echo.returncode}")
        require(
            echo.stdout == (tests_dir / "echo.expected").read_bytes(),
            "echo test: unexpected output",
        )

        unmatched = run_program(executable, tests_dir / "unmatched.bf", timeout=2)
        require_error(unmatched, "unmatched bracket", "unmatched bracket test")

        unmatched_close = run_program(
            executable, tests_dir / "unmatched-close.bf", timeout=2
        )
        require_error(
            unmatched_close,
            "unmatched bracket",
            "unmatched closing bracket test",
        )

        bounds = run_program(executable, tests_dir / "bounds.bf")
        require_error(bounds, "data pointer out of bounds", "bounds test")

        upper_bound_program = temporary_path / "upper-bound.bf"
        upper_bound_program.write_bytes(b">" * 30001)
        upper_bound = run_program(executable, upper_bound_program)
        require_error(
            upper_bound,
            "data pointer out of bounds",
            "upper bounds test",
        )

        underflow_program = temporary_path / "underflow.bf"
        underflow_program.write_bytes(b"-.")
        underflow = run_program(executable, underflow_program)
        require(
            underflow.returncode == 0,
            f"cell underflow test: exit status {underflow.returncode}",
        )
        require(underflow.stdout == b"\xff", "cell underflow test: expected byte 255")

        overflow_program = temporary_path / "overflow.bf"
        overflow_program.write_bytes(b"+" * 256 + b".")
        overflow = run_program(executable, overflow_program)
        require(
            overflow.returncode == 0,
            f"cell overflow test: exit status {overflow.returncode}",
        )
        require(overflow.stdout == b"\x00", "cell overflow test: expected byte 0")

        usage = run_program(executable)
        require_error(usage, "usage: brainfuck", "usage test")

        missing = run_program(executable, temporary_path / "does-not-exist.bf")
        require_error(missing, "cannot open program file", "missing file test")


if __name__ == "__main__":
    try:
        main()
    except TestFailure as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from error
