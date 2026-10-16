"""Install, backup, restore, format and info flows. Interaction goes through Console only."""

from __future__ import annotations

import os
import shutil
import tempfile
from dataclasses import dataclass, field
from typing import Dict, List, Optional

from . import cdc, device
from .cdc import DeviceRefused, ProtocolError
from .device import Cancelled, FlashError, Hardware, Orbit12Device
from .github import DEFAULT_REPO, Release, ReleaseError, parse_releases, releases_url, select_release
from .interfaces import Downloader
from .package import (MANIFEST_NAME, MAX_IMAGE_BYTES, MAX_MANIFEST_BYTES, MAX_SUMS_BYTES, SUMS_NAME,
                      Manifest, PackageError, parse_manifest, parse_sha256sums,
                      tag_matches_version, verify_image_bytes)


@dataclass
class Options:
    repo: str = DEFAULT_REPO
    include_prereleases: bool = False
    tag: Optional[str] = None
    uf2: Optional[str] = None
    manifest: Optional[str] = None
    sums: Optional[str] = None
    port: Optional[str] = None
    backup_root: Optional[str] = None
    repo_confirmed: bool = False
    bootsel_timeout_s: float = 30.0
    reenum_timeout_s: float = 30.0
    drive_gone_timeout_s: float = 30.0


@dataclass
class Package:
    manifest: Manifest
    image: bytes
    source: str
    release: Optional[Release] = None
    sums: Dict[str, str] = field(default_factory=dict)


def require_repo_trust(hw: Hardware, opts: Options) -> None:
    """Non-default repositories run their own build output. Typing the name is required, even with --yes."""
    if opts.repo.lower() == DEFAULT_REPO.lower() or opts.repo_confirmed:
        return
    hw.console.warn(
        f"you asked for the repository {opts.repo!r}, which is NOT the official {DEFAULT_REPO}. "
        "Its firmware is trusted code that will be written to your board. Checksums prove that "
        "the files match each other, not who published them.")
    if not hw.console.confirm_typed(opts.repo, f"Type the repository name {opts.repo} to trust it"):
        raise Cancelled("repository not confirmed; nothing was downloaded or flashed")
    opts.repo_confirmed = True


def list_releases(hw: Hardware, downloader: Downloader, opts: Options) -> List[Release]:
    require_repo_trust(hw, opts)
    data = downloader.fetch(releases_url(opts.repo), 2 * 1024 * 1024,
                            token=os.environ.get("GITHUB_TOKEN") or None,
                            accept="application/vnd.github+json")
    return parse_releases(data)


def _read_local(path: str, limit: int) -> bytes:
    with open(path, "rb") as handle:
        data = handle.read(limit + 1)
    if len(data) > limit:
        raise PackageError(f"{os.path.basename(path)} is larger than the installer accepts")
    return data


def load_local_package(opts: Options) -> Package:
    """Offline mode: --uf2 PATH --manifest PATH [--sums PATH]. Verifies before anything is written."""
    if not opts.uf2 or not opts.manifest:
        raise PackageError("offline installs need both --uf2 and --manifest")
    manifest = parse_manifest(_read_local(opts.manifest, MAX_MANIFEST_BYTES))
    sums = None
    if opts.sums:
        sums = parse_sha256sums(_read_local(opts.sums, MAX_SUMS_BYTES))
    image = _read_local(opts.uf2, MAX_IMAGE_BYTES)
    verify_image_bytes(image, manifest, sums)
    return Package(manifest, image, source=f"local files ({os.path.basename(opts.uf2)})",
                   sums=sums or {})


def download_package(hw: Hardware, downloader: Downloader, release: Release, opts: Options) -> Package:
    """Stages the release files in a temp directory, then verifies them from memory."""
    require_repo_trust(hw, opts)
    token = os.environ.get("GITHUB_TOKEN") or None
    manifest_asset = release.asset(MANIFEST_NAME)
    sums_asset = release.asset(SUMS_NAME)
    if manifest_asset is None or sums_asset is None:
        raise ReleaseError(f"release {release.tag} lacks {MANIFEST_NAME} or {SUMS_NAME}; refusing it")
    stage = tempfile.mkdtemp(prefix="orbit12-install-")
    try:
        manifest_bytes = downloader.fetch(manifest_asset.url, MAX_MANIFEST_BYTES, token)
        _stage(stage, MANIFEST_NAME, manifest_bytes)
        manifest = parse_manifest(manifest_bytes, expected_repo=opts.repo)
        if not tag_matches_version(release.tag, manifest.version):
            raise PackageError(f"release tag {release.tag} does not match manifest version {manifest.version}")
        if manifest.prerelease != release.prerelease:
            raise PackageError("the manifest prerelease flag disagrees with the GitHub release")
        image_asset = release.asset(manifest.image.file)
        if image_asset is None:
            raise ReleaseError(f"release {release.tag} has no {manifest.image.file}; refusing it")
        if image_asset.size and image_asset.size != manifest.image.size:
            raise PackageError("the release asset size disagrees with the manifest")
        sums_bytes = downloader.fetch(sums_asset.url, MAX_SUMS_BYTES, token)
        _stage(stage, SUMS_NAME, sums_bytes)
        sums = parse_sha256sums(sums_bytes)
        image = downloader.fetch(image_asset.url, MAX_IMAGE_BYTES, token)
        _stage(stage, manifest.image.file, image)
        verify_image_bytes(image, manifest, sums)
        hw.console.say(f"Verified {manifest.image.file}: SHA-256, SHA256SUMS and UF2 structure OK.")
        return Package(manifest, image, source=f"release {release.tag}", release=release, sums=sums)
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def _stage(folder: str, name: str, data: bytes) -> None:
    # Names are validated by parse_manifest and parse_sha256sums (bare file names only).
    with open(os.path.join(folder, name), "xb") as handle:
        handle.write(data)


def pick_release(hw: Hardware, downloader: Downloader, opts: Options, interactive: bool) -> Release:
    releases = list_releases(hw, downloader, opts)
    if opts.tag:
        for release in releases:
            if release.tag == opts.tag:  # an explicit tag is an explicit choice, prerelease or not
                return release
        raise ReleaseError(f"no published release is tagged {opts.tag!r}")
    try:
        chosen = select_release(releases, opts.include_prereleases)
    except ReleaseError as error:
        raise ReleaseError(str(error)) from None
    hw.console.say(f"Latest {'prerelease' if chosen.prerelease else 'stable release'}: "
                   f"{chosen.name} ({chosen.tag}) published {chosen.published_at[:10]}")
    if chosen.body.strip():
        hw.console.say(chosen.body.strip()[:1200])
    if interactive and not hw.console.confirm(f"Install {chosen.tag}?", default=True):
        raise Cancelled("install cancelled")
    return chosen


# ---- flashing ------------------------------------------------------------------------------

def _leave_maintenance_quietly(hw: Hardware, device_path: str) -> None:
    try:
        device.exit_maintenance(hw, device_path)
    except FlashError as error:
        hw.console.warn(str(error))


def _take_backup(hw: Hardware, orbit: Optional[Orbit12Device], opts: Options) -> Optional[str]:
    """Backup before any write. Returns the folder, or None when nothing could be saved."""
    root = opts.backup_root or device.default_backup_root()
    if orbit is None:
        folder = device.copy_circuitpy_userdata(hw, root)
        if folder:
            hw.console.say(f"Copied CIRCUITPY userdata to {folder} (read only).")
        else:
            hw.console.warn("no Orbit12 board answered and no CIRCUITPY userdata was found to copy")
        return folder
    device_path = device.ensure_maintenance(hw, orbit)
    if device_path is None:
        hw.console.warn("could not enter maintenance mode, so no scene backup was made")
        return None
    folder = None
    try:
        folder = device.backup_device(hw, device_path, root)
        if folder:
            hw.console.say(f"Backup written to {folder}")
    except (FlashError, DeviceRefused, TimeoutError, ProtocolError, OSError) as error:
        hw.console.warn(f"backup failed: {error}")
        folder = None
    finally:
        _leave_maintenance_quietly(hw, device_path)
    return folder


def run_flash(hw: Hardware, package: Package, opts: Options, interactive: bool = True) -> str:
    """Backup, BOOTSEL, copy, wait, verify, and repair an unformatted partition. Returns a summary."""
    manifest = package.manifest
    hw.console.say(f"Flashing Orbit12 {manifest.version} (git {manifest.git_sha}, api {manifest.engine_api}) "
                   f"from {package.source}.")
    orbit = device.find_orbit12(hw, opts.port)
    if orbit is not None:
        hw.console.say(f"Found Orbit12 on {orbit.port.device}.")
    backup_dir = _take_backup(hw, orbit, opts)
    # Only a maintenance backup is restored automatically. A CIRCUITPY copy is kept for the user.
    restore_from = backup_dir if orbit is not None else None
    if backup_dir is None and not hw.console.confirm_typed(
            "FLASH", "No backup was made. The image does not overwrite the scene partition, but "
            "without a backup a later format would lose the scenes. Type FLASH to continue anyway"):
        raise Cancelled("flash cancelled before any write")
    if interactive and not hw.console.confirm("Put the board in the bootloader and flash now?", default=True):
        raise Cancelled("flash cancelled before any write")
    if backup_dir:
        # Keep the verified image beside the backup so a failed copy can be recovered by hand.
        _stash_image(backup_dir, package)
    touch_port = orbit.port.device if orbit else None
    if orbit is None:
        lone = device.lone_cdc_port(hw)
        if lone and hw.console.confirm(
                f"Reset {lone} into the bootloader over USB? (a CircuitPython board accepts this)",
                default=True):
            touch_port = lone
    device.request_bootsel(hw, touch_port)
    volume = device.wait_for_bootsel(hw, opts.bootsel_timeout_s)
    hw.console.say(f"Bootloader drive: {volume.path}")
    try:
        result = device.copy_image(volume, package.image)
    except device.CopyInterrupted as error:
        raise FlashError(f"{error} {_recovery_text(backup_dir, package)}") from None
    if result.pending:
        hw.console.say("The drive disappeared as the last block landed; waiting for the board to restart.")
    if not device.wait_for_drive_gone(hw, volume.path, opts.drive_gone_timeout_s):
        raise FlashError("the image was copied but the bootloader drive is still present; the board "
                         "did not restart. " + _recovery_text(backup_dir, package))
    dev = device.wait_for_reenumeration(hw, opts.reenum_timeout_s)
    if dev is None:
        raise FlashError("the board did not come back as an Orbit12 device within the timeout. "
                         + _recovery_text(backup_dir, package))
    for warning in device.verify_firmware(dev, manifest):
        hw.console.warn(warning)
    hw.console.say("Firmware verified on the board.")
    if dev.storage_mounted is False:
        _repair_partition(hw, dev, restore_from, interactive)
    elif dev.storage_mounted:
        hw.console.say("Scene storage is mounted; saved scenes are unchanged.")
    if backup_dir:
        hw.console.say(f"Backup kept at {backup_dir}")
    return f"Orbit12 {manifest.version} is installed on {dev.port.device}."


def _stash_image(backup_dir: str, package: Package) -> None:
    target = os.path.join(backup_dir, package.manifest.image.file)
    with open(target, "wb") as handle:
        handle.write(package.image)


def _recovery_text(backup_dir: Optional[str], package: Package) -> str:
    text = ("Recovery: hold the BOOT button (see Adafruit's MacroPad RP2040 guide) while connecting the "
            "board so the RPI-RP2 drive appears, copy the firmware file to it by hand, and run the "
            "installer again.")
    if backup_dir:
        text += (f" The verified firmware file ({package.manifest.image.file}) and your backup are in "
                 f"{backup_dir}.")
    else:
        text += f" The verified firmware file is {package.manifest.image.file} in the release."
    return text


def _repair_partition(hw: Hardware, dev: Orbit12Device, backup_dir: Optional[str], interactive: bool) -> None:
    hw.console.warn("The scene partition is not mounted. It was never formatted for Orbit12 (for "
                    "example it still holds CircuitPython data), so the board runs on stock settings.")
    if not hw.console.confirm(
            "Format the partition now so scenes can be saved? This erases every file on it.",
            default=False):
        hw.console.warn("Left unformatted. Run `orbit12-installer format` when ready; the board keeps running.")
        return
    device_path = device.ensure_maintenance(hw, dev)
    if device_path is None:
        raise FlashError("maintenance mode was not reached; run `orbit12-installer format` later")
    device.format_device(hw, device_path)
    if backup_dir:
        try:
            restored = device.restore_device(hw, device_path, backup_dir)
        except (FlashError, DeviceRefused, ProtocolError, TimeoutError, OSError) as error:
            _leave_maintenance_quietly(hw, device_path)
            raise FlashError(
                f"the firmware is installed and the partition is formatted, but the restore stopped: "
                f"{error}. Your backup is still in {backup_dir}; after the board restarts, run "
                f"`orbit12-installer restore --from \"{backup_dir}\"`.") from None
        hw.console.say(f"Restored {len(restored)} file(s) from the backup.")
    finish_and_reboot(hw, device_path)


def finish_and_reboot(hw: Hardware, device_path: str) -> None:
    """Leaves maintenance with Q, asks for a power-cycle, and checks that storage is mounted."""
    device.exit_maintenance(hw, device_path)
    hw.console.pause_for_action("Unplug the board and plug it back in so it starts normally.")
    dev = device.wait_for_reenumeration(hw, 30.0)
    if dev is None:
        raise FlashError("the board did not come back after the power-cycle; check it and run the installer again")
    if dev.storage_mounted is not True:
        raise FlashError("storage is still not mounted after the power-cycle; run `format` again")
    hw.console.say("Storage is mounted. Scenes will load at this start.")


# ---- commands that do not flash ------------------------------------------------------------

def info(hw: Hardware, opts: Options) -> str:
    orbit = device.find_orbit12(hw, opts.port)
    if orbit is None:
        return "No Orbit12 board answered on a USB serial port."
    lines = [f"port: {orbit.port.device}"]
    report = orbit.info
    if report is not None:
        lines.append(f"firmware: fw={report.fw} sha={report.sha} engine_api={report.api} board={report.board}")
        lines.append(f"modules: count={report.module_count} core={','.join(report.core_modules) or '-'} "
                     f"user={','.join(report.user_modules) or '-'}")
        lines.append(f"storage: mounted={int(report.storage_mounted)} scenes={report.scenes} "
                     f"names_ok={int(report.names_ok)}")
        lines.append(f"state: maintenance={int(report.maintenance)} playback={int(report.transport)}")
    else:
        lines.append("firmware: no INFO reply (older firmware)")
        status = orbit.status
        if status.version is not None:
            v = status.version
            lines.append(f"firmware: fw={v.semver} git={v.git_sha} engine_api={v.api} board={v.board}")
        elif status.version_problem:
            lines.append(f"firmware: {status.version_problem}")
        else:
            lines.append("firmware: no VERSION line")
        lines.append(f"storage mounted: {orbit.storage_mounted}")
    return "\n".join(lines)


def backup_only(hw: Hardware, opts: Options) -> str:
    orbit = device.find_orbit12(hw, opts.port)
    if orbit is None:
        raise FlashError("no Orbit12 board answered; connect it and try again")
    folder = _take_backup(hw, orbit, opts)
    if folder is None:
        raise FlashError("no backup was written")
    return f"Backup written to {folder}"


def restore_only(hw: Hardware, opts: Options, folder: str) -> str:
    device.load_backup(folder)  # validate before any board interaction
    orbit = device.find_orbit12(hw, opts.port)
    if orbit is None:
        raise FlashError("no Orbit12 board answered; connect it and try again")
    device_path = device.ensure_maintenance(hw, orbit)
    if device_path is None:
        raise FlashError("maintenance mode was not reached; nothing was restored")
    # Restoring replaces the scenes and settings currently on the board: destructive.
    hw.console.warn("restore overwrites the scenes, scene names, active-scene pointer and user "
                    "settings that are on the board now with the backup's copies.")
    if not hw.console.confirm_typed("ERASE", "Type ERASE to overwrite the board's files with the backup"):
        _leave_maintenance_quietly(hw, device_path)
        raise Cancelled("restore cancelled; nothing was written")
    try:
        restored = device.restore_device(hw, device_path, folder)
    except DeviceRefused as error:
        _leave_maintenance_quietly(hw, device_path)
        raise FlashError(f"restore stopped: {error}") from None
    finish_and_reboot(hw, device_path)
    return f"Restored {', '.join(restored) or 'nothing'} from {folder}."


def format_only(hw: Hardware, opts: Options) -> str:
    orbit = device.find_orbit12(hw, opts.port)
    if orbit is None:
        raise FlashError("no Orbit12 board answered; connect it and try again")
    device_path = device.ensure_maintenance(hw, orbit)
    if device_path is None:
        raise FlashError("maintenance mode was not reached; nothing was formatted")
    device.format_device(hw, device_path)
    finish_and_reboot(hw, device_path)
    return "Partition formatted and mounted."
