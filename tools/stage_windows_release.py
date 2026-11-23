"""Stage a hash-checked Windows x64 PC bundle from verified upstream artifacts."""
from argparse import ArgumentParser
from hashlib import file_digest
from pathlib import Path
import re
from shutil import copyfile
import subprocess
import sys
import tarfile
from tempfile import TemporaryDirectory
from zipfile import ZipFile

LIBUSB = {
    "libusb-1.0.29.7z": (
        "964a38152ca9a104cd00ec8d2f0617b89cd814f9b635e29763c68563d951521d",
        "https://github.com/libusb/libusb/releases/download/v1.0.29/libusb-1.0.29.7z",
    ),
    "libusb-source.tar.gz": (
        "4ae78a9c6c540a3ccd7153c77e24947fa2ba50d845714bbbfccf8783832ed704",
        "https://codeload.github.com/libusb/libusb/tar.gz/15a7ebb4d426c5ce196684347d2b7cafad862626",
    ),
}


def digest(path):
    with path.open("rb") as stream:
        return file_digest(stream, "sha256").hexdigest()


def locked_wheels(wheels, lock):
    expected = {}
    for line in lock.read_text().splitlines():
        if line.startswith("#") or not line:
            continue
        match = re.fullmatch(r"([\w-]+)==([^ ]+) --hash=sha256:([0-9a-f]{64})", line)
        name, version, checksum = match.groups()
        expected[(name, version)] = checksum
    selected = []
    for wheel in sorted(wheels.glob("*.whl")):
        normalized, version, _ = wheel.name.split("-", 2)
        key = (re.sub(r"[-_.]+", "-", normalized).lower(), version)
        if key not in expected:
            raise ValueError(f"unexpected wheel: {wheel.name}")
        if digest(wheel) != expected.pop(key):
            raise ValueError(f"wheel hash mismatch: {wheel.name}")
        selected.append(wheel)
    if expected:
        raise ValueError(f"missing wheels: {', '.join(name for name, _ in expected)}")
    return selected


def local_libusb(cache, output, seven_zip):
    paths = {}
    for name, (checksum, url) in LIBUSB.items():
        target = cache / name
        if not target.is_file():
            subprocess.run(["curl", "-fsSL", "--retry", "2", url, "-o", str(target)],
                           check=True)
        if digest(target) != checksum:
            raise ValueError(f"libusb hash mismatch: {target}")
        paths[name] = target

    (output / "bin").mkdir(exist_ok=True)
    with (output / "bin/libusb-1.0.dll").open("wb") as dll:
        subprocess.run([seven_zip, "x", "-so", str(paths["libusb-1.0.29.7z"]),
                        "MinGW64/dll/libusb-1.0.dll"], stdout=dll, check=True)
    sources = output / "sources"
    sources.mkdir(exist_ok=True)
    copyfile(paths["libusb-source.tar.gz"], sources / "libusb-1.0.29.tar.gz")
    with tarfile.open(paths["libusb-source.tar.gz"]) as archive:
        copying = next(item for item in archive if item.name.endswith("/COPYING"))
        (output / "licenses/libusb-COPYING.txt").write_bytes(archive.extractfile(copying).read())


def licenses(wheel, directory):
    with ZipFile(wheel) as archive:
        members = []
        for name in archive.namelist():
            entry = Path(name)
            if not name.endswith("/") and (
                ".dist-info/licenses/" in name.lower() or
                entry.name.upper().startswith(("LICENSE", "COPYING", "NOTICE"))
            ):
                members.append(name)
        if members:
            directory.mkdir(exist_ok=True)
            for index, name in enumerate(members):
                local = directory / f"{index:03d}-{Path(name).name}"
                local.write_bytes(archive.read(name))


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mock", type=Path, required=True)
    parser.add_argument("--sdk-wheel", type=Path, required=True)
    parser.add_argument("--wheels", type=Path, help="prepopulated locked Windows wheel directory")
    parser.add_argument("--libusb-cache", type=Path)
    parser.add_argument("--seven-zip", default="7z")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    lock = root / "pc/requirements-windows-cp312.lock"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists():
        parser.error(f"output already exists: {args.output}")
    with TemporaryDirectory(dir=args.output.parent) as scratch:
        staging = Path(scratch) / "bundle"
        staging.mkdir()
        count = stage(args, staging, root, lock)
        staging.rename(args.output)
    print(f"{count} files verified and recorded in {args.output / 'SHA256SUMS'}")


def stage(args, staging, root, lock):
    (staging / "wheels").mkdir()
    (staging / "licenses").mkdir()
    with TemporaryDirectory() as scratch:
        cache = Path(scratch)
        wheels = args.wheels or (cache / "wheels")
        libusb_cache = args.libusb_cache or cache
        if args.wheels is None:
            subprocess.run([sys.executable, "-m", "pip", "download", "--platform", "win_amd64",
                            "--python-version", "3.12", "--implementation", "cp", "--abi", "cp312",
                            "--only-binary=:all:", "--no-deps", "--require-hashes", "-r", str(lock),
                            "-d", str(wheels)], check=True)
        for wheel in [*locked_wheels(wheels, lock), args.sdk_wheel]:
            copyfile(wheel, staging / "wheels" / wheel.name)
            licenses(wheel, staging / "licenses" / wheel.name.split("-", 1)[0])
        local_libusb(libusb_cache, staging, args.seven_zip)
    copyfile(lock, staging / "requirements.lock")
    copyfile(args.mock, staging / "mock-device.exe")
    copyfile(root / "tools/windows_offline.ps1", staging / "usb2xxx.ps1")
    copyfile(root / "docs/windows-offline.md", staging / "README.md")
    manifest = []
    for path in sorted(staging.rglob("*")):
        if path.is_file():
            manifest.append(f"{digest(path)}  {path.relative_to(staging).as_posix()}")
    (staging / "SHA256SUMS").write_text("\n".join(manifest) + "\n")
    return len(manifest)


if __name__ == "__main__":
    main()
