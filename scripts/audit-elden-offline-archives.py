#!/usr/bin/env python3
"""Verify downloaded Elden archives against their build records, without a device.

An accepted archive is a build input, not evidence of working hardware. This
does not link it, change a component selection, or invoke a device tool.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def object_info(data):
    require(len(data) >= 32, "short Mach-O header")
    magic, cpu, _, kind, count, command_bytes, _, _ = struct.unpack_from("<8I", data)
    require((magic, cpu, kind) == (0xFEEDFACF, 0x100000C, 1), "expected ARM64 MH_OBJECT")
    end = 32 + command_bytes
    require(end <= len(data), "load commands extend beyond object")
    offset, version, initializers = 32, None, []
    for _ in range(count):
        require(offset + 8 <= end, "short load command")
        command, size = struct.unpack_from("<II", data, offset)
        require(size >= 8 and size % 8 == 0 and offset + size <= end, "invalid load command size")
        if command == 0x32:  # LC_BUILD_VERSION
            require(size >= 24 and version is None, "invalid or duplicate build version")
            version = struct.unpack_from("<II", data, offset + 8)
        if command == 0x19:  # LC_SEGMENT_64
            require(size >= 72, "short segment command")
            sections = struct.unpack_from("<I", data, offset + 64)[0]
            require(72 + sections * 80 == size, "invalid section table")
            for index in range(sections):
                start = offset + 72 + index * 80
                name = data[start:start + 16].split(b"\0", 1)[0].decode("ascii")
                segment = data[start + 16:start + 32].split(b"\0", 1)[0].decode("ascii")
                if name == "__mod_init_func":
                    initializers.append(f"{segment},{name}")
        offset += size
    require(offset == end, "load command count differs from recorded size")
    require(version == (2, 0x110000), f"expected iOS17.0 build version, got {version}")
    return {"bytes": len(data), "sha256": digest(data), "ios_minimum": "17.0",
            "initializer_sections": initializers}


def archive_info(path):
    raw = path.read_bytes()
    require(raw.startswith(b"!<arch>\n"), f"{path}: invalid archive magic")
    offset, members, metadata, names = 8, [], [], set()
    while offset < len(raw):
        require(offset + 60 <= len(raw), "truncated archive header")
        header = raw[offset:offset + 60]
        require(header[58:] == b"`\n", "invalid archive header terminator")
        length = int(header[48:58].decode("ascii").strip())
        require(length >= 0 and offset + 60 + length <= len(raw), "archive member extends past end")
        data = raw[offset + 60:offset + 60 + length]
        name = header[:16].decode("ascii").strip()
        if name.startswith("#1/"):  # Darwin/BSD long name, including name padding
            name_length = int(name[3:])
            require(0 < name_length <= len(data), "invalid extended member name")
            name = data[:name_length].rstrip(b"\0").decode("utf-8")
            data = data[name_length:]
        else:
            name = name.rstrip("/")
        fields = {"name": name, "mtime": int(header[16:28].strip() or b"0"),
                  "uid": int(header[28:34].strip() or b"0"),
                  "gid": int(header[34:40].strip() or b"0")}
        if name.startswith("__.SYMDEF") or not name:
            metadata.append(fields)
        else:
            require(name not in names and name.endswith(".o"), f"unexpected or duplicate member {name}")
            names.add(name)
            members.append(dict(fields, **object_info(data)))
        offset += 60 + length
        if offset % 2:
            require(offset < len(raw) and raw[offset:offset + 1] == b"\n", "invalid archive padding")
            offset += 1
    require(offset == len(raw) and members, "empty archive or trailing data")
    return {"file": str(path.resolve()), "sha256": digest(raw), "bytes": len(raw),
            "members": members, "metadata": metadata,
            "zero_timestamps_ids": all(not (m["mtime"] or m["uid"] or m["gid"])
                                       for m in members + metadata)}


def audit_original_accelerator(kernel, plan):
    raw = kernel.read_bytes()
    require(len(raw) >= 32 and struct.unpack_from("<II", raw) == (0xFEEDFACF, 0x100000C),
            "original binding target is not ARM64 Mach-O")
    count, size = struct.unpack_from("<II", raw, 16)
    end, offset, segments = 32 + size, 32, []
    require(end <= len(raw), "target load commands exceed file")
    for _ in range(count):
        require(offset + 8 <= end, "short target load command")
        command, length = struct.unpack_from("<II", raw, offset)
        require(length >= 8 and offset + length <= end, "invalid target load command")
        if command == 0x19:
            require(length >= 72, "short target segment")
            va, _, fileoff, filesize = struct.unpack_from("<4Q", raw, offset + 24)
            require(fileoff + filesize <= len(raw), "target segment extends past file")
            segments.append((va, fileoff, filesize))
        offset += length
    require(offset == end, "target load command size differs")
    entries = {}
    for address, expected in plan["original_entry_sha256"].items():
        va = int(address, 16)
        regions = [(off + va - base) for base, off, size in segments if base <= va and va + 32 <= base + size]
        require(len(regions) == 1, f"original accelerator address is not uniquely mapped: {address}")
        actual = digest(raw[regions[0]:regions[0] + 32])
        require(actual == expected, f"original accelerator instructions differ: {address}")
        entries[address] = {"bytes": 32, "sha256": actual}
    require(plan["instance_bytes"] == 136 and plan["virtual_entries"] == 168
            and plan["operational_overrides"] == [], "unexpected original accelerator contract")
    return {"kernel": str(kernel.resolve()), "kernel_sha256": digest(raw), "entry_checks": entries,
            "instance_bytes": 136, "virtual_entries": 168,
            "limits": "Instruction identity confirms these imports; final archive linking and runtime publication remain untested."}


def audit(cores, source_manifest, gpu, kernel):
    built, sources = load(cores / "result.json"), load(source_manifest)
    require(built["source_manifest_sha256"] == digest(source_manifest.read_bytes()), "source manifest differs")
    require(built["profile"] == sources["profile"] == "arm64-apple-ios17.0", "unexpected profile")
    require(built["device_operations"] is False and built["hardware_tested"] is False,
            "build record contains device/hardware claims")
    require(sources["hardware_enabled"] is False, "source packet enables hardware")
    require(len({g["name"] for g in sources["groups"]}) == len(sources["groups"]), "duplicate source group")
    expected_groups = {g["name"]: g for g in sources["groups"]}
    require(set(expected_groups) == {g["name"] for g in built["groups"]}, "group inventory differs")
    result = {"profile": built["profile"], "source_manifest_sha256": built["source_manifest_sha256"],
              "archives": [], "device_operations": False, "linked": False, "hardware_tested": False}
    for group in built["groups"]:
        require(group["hardware_enabled"] is False and group["automatic_initializers"] is False,
                f"{group['name']}: not a build-only core")
        for source in expected_groups[group["name"]]["files"]:
            path = source_manifest.parent / group["name"] / source["file"]
            require(digest(path.read_bytes()) == source["sha256"], f"source bytes differ: {path}")
        record = archive_info(cores / group["name"] / Path(group["archive"]).name)
        require(record["sha256"] == group["archive_sha256"] and record["bytes"] == group["archive_bytes"],
                f"{group['name']}: archive digest/size differs")
        actual = {member["name"]: member["sha256"] for member in record["members"]}
        require(actual == group["objects"], f"{group['name']}: archived object bytes differ")
        require(record["zero_timestamps_ids"], f"{group['name']}: nondeterministic member metadata")
        require(not any(member["initializer_sections"] for member in record["members"]),
                f"{group['name']}: automatic initializer found")
        record["name"] = group["name"]
        result["archives"].append(record)
    gpu_built = load(gpu / "build-report.json")
    record = archive_info(gpu / "libAdreno830.a")
    require(record["sha256"] == gpu_built["archive_sha256"], "GPU archive digest differs")
    require(sorted(m["name"] for m in record["members"]) == sorted(gpu_built["objects"]),
            "GPU object inventory differs")
    plan = gpu / "ioaccelerator-base-abi.json"
    require(digest(plan.read_bytes()) == gpu_built["accelerator_abi_sha256"], "original accelerator ABI plan differs")
    require("NativeAdrenoAccelerator.late.o" in gpu_built["objects"], "missing staged accelerator object")
    require(not any(m["initializer_sections"] for m in record["members"]
                    if m["name"] == "NativeAdrenoAccelerator.late.o"), "accelerator initializer was not staged")
    original = audit_original_accelerator(kernel, load(plan))
    record.update(name="Adreno830", unresolved_import_count=len(gpu_built["undefined"]),
                  original_accelerator_abi_sha256=gpu_built["accelerator_abi_sha256"],
                  original_accelerator=original,
                  limits=["Final resolution of all kernel imports remains pending.",
                          "Board firmware/power/SMMU ownership and final linking are not qualified.",
                          "Context banks must be identified by controller base plus bank. GPU@03da0000 and UFS@15000000 are separate; equal CB numbers do not imply overlap."])
    result["archives"].append(record)
    result["accepted"] = True
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cores", type=Path, required=True)
    parser.add_argument("--source-manifest", type=Path, required=True)
    parser.add_argument("--gpu", type=Path, required=True)
    parser.add_argument("--kernel", type=Path, required=True, help="Elden kernel for original accelerator instruction comparison")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = audit(args.cores, args.source_manifest, args.gpu, args.kernel)
    except (ValueError, OSError, KeyError, struct.error) as error:
        parser.exit(1, f"archive audit refused: {error}\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"accepted": True,
                      "archives": [{"name": r["name"], "objects": len(r["members"]),
                                    "bytes": r["bytes"], "sha256": r["sha256"]}
                                   for r in result["archives"]],
                      "linked": False, "hardware_tested": False}, indent=2))


if __name__ == "__main__":
    main()
