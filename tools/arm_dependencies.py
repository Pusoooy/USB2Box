"""Fetch or verify the pinned ARM build dependencies."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tarfile
import tempfile


MEMBERS = {
    "cmsis-core": ("CMSIS/Core/Include", "LICENSE.txt"),
    "cmsis-device": ("Include", "Source/Templates/system_stm32f4xx.c", "LICENSE.md"),
    "hal": ("Inc", "Src", "LICENSE.md"),
}


def verify_digest(path, expected):
    with path.open("rb") as archive:
        if hashlib.file_digest(archive, "sha256").hexdigest() != expected:
            raise ValueError(f"SHA-256 mismatch: {path}")


def sync_files(archive, directory, prefixes, extract):
    expected = set()
    with tarfile.open(archive, "r|gz") as source:
        for member in source:
            _, _, relative = member.name.partition("/")
            if not any(relative == prefix or relative.startswith(prefix + "/")
                       for prefix in prefixes):
                continue
            if Path(relative).is_absolute() or ".." in Path(relative).parts:
                raise ValueError(f"Invalid archive path: {relative}")
            if member.isdir():
                continue
            if not member.isfile():
                raise ValueError(f"Expected a regular file: {relative}")
            expected.add(relative)
            with source.extractfile(member) as file:
                contents = file.read()
            target = directory / relative
            if extract:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(contents)
            elif target.is_symlink() or target.read_bytes() != contents:
                raise ValueError(f"Dependency content mismatch: {target}")
    actual = {file.relative_to(directory).as_posix() for file in directory.rglob("*")
              if file.is_file() or file.is_symlink()}
    if not expected or actual != expected:
        raise ValueError(f"Dependency file set mismatch: {directory}")


def prepare(root, component, digest, url, fetch):
    archive = root / f"{component}.tar.gz"
    directory = root / component
    if fetch and not archive.is_file():
        with tempfile.TemporaryDirectory(dir=root) as scratch:
            download = Path(scratch) / archive.name
            subprocess.run(["curl", "--fail", "--silent", "--show-error", "--location",
                            "--retry", "2", "-o", str(download), url], check=True)
            verify_digest(download, digest)
            download.replace(archive)
    if not archive.is_file():
        raise ValueError(f"Missing {archive}. Run make fetch-deps or supply the locked archives.")
    verify_digest(archive, digest)
    if fetch and not directory.exists():
        with tempfile.TemporaryDirectory(dir=root) as scratch:
            candidate = Path(scratch) / component
            sync_files(archive, candidate, MEMBERS[component], extract=True)
            candidate.rename(directory)
    else:
        sync_files(archive, directory, MEMBERS[component], extract=False)
    print(f"{component}: archive SHA-256 and source files verified")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("fetch", "verify"))
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    fetch = args.mode == "fetch"
    lock = Path(__file__).with_name("arm-dependencies.lock")
    try:
        if fetch:
            args.root.mkdir(parents=True, exist_ok=True)
        for line in lock.read_text().splitlines():
            if line.strip() and not line.lstrip().startswith("#"):
                component, digest, url = line.split()
                prepare(args.root, component, digest, url, fetch)
    except (OSError, ValueError, subprocess.SubprocessError, tarfile.TarError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
