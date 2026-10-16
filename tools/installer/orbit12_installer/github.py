"""GitHub release discovery and bounded HTTPS downloads (urllib only).

Release selection: the releases list (not /latest, which skips prereleases) is filtered to
non-draft entries, sorted newest first by published date, and stable releases are preferred
unless the caller asks for prereleases. A missing stable release is an explicit error with a
hint, never a silent fallback.

Downloads: https only, a host allowlist for every redirect hop, a bounded redirect count,
streamed size enforcement, and the optional GITHUB_TOKEN is sent only to api.github.com and
never after a redirect.
"""

from __future__ import annotations

import datetime as _dt
import json
import re
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import List, Optional, Sequence

from .interfaces import Downloader
from .package import PackageError, safe_name

DEFAULT_REPO = "handcraftedcc/Orbit12"
API_HOST = "api.github.com"
ALLOWED_HOSTS = frozenset({
    "api.github.com",
    "github.com",
    "objects.githubusercontent.com",
    "release-assets.githubusercontent.com",
    "github-releases.githubusercontent.com",
})
MAX_RELEASE_JSON_BYTES = 2 * 1024 * 1024
MAX_REDIRECTS = 5
TIMEOUT_S = 30.0
_REPO = re.compile(r"^[A-Za-z0-9._-]+/[A-Za-z0-9._-]+$")


class ReleaseError(Exception):
    """User-facing release discovery or download failure."""


class NoStableRelease(ReleaseError):
    pass


@dataclass(frozen=True)
class Asset:
    name: str
    url: str
    size: int


@dataclass(frozen=True)
class Release:
    tag: str
    name: str
    prerelease: bool
    published_at: str
    body: str
    assets: tuple

    def asset(self, name: str) -> Optional[Asset]:
        for item in self.assets:
            if item.name == name:
                return item
        return None


def releases_url(repo: str, per_page: int = 100) -> str:
    if _REPO.match(repo) is None:
        raise ReleaseError(f"repository {repo!r} must look like owner/name")
    return f"https://{API_HOST}/repos/{repo}/releases?per_page={per_page}"


def check_url(url: str) -> str:
    parts = urllib.parse.urlsplit(url)
    if parts.scheme != "https" or parts.hostname not in ALLOWED_HOSTS:
        raise ReleaseError(f"refusing to download from {parts.hostname or url!r}")
    return url


def _parse_time(value: str) -> _dt.datetime:
    try:
        return _dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
    except (AttributeError, ValueError):
        return _dt.datetime.min.replace(tzinfo=_dt.timezone.utc)


def parse_releases(data: bytes) -> List[Release]:
    if len(data) > MAX_RELEASE_JSON_BYTES:
        raise ReleaseError("release list is larger than the installer accepts")
    try:
        items = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as error:
        raise ReleaseError(f"release list is not valid JSON: {error}") from None
    if not isinstance(items, list):
        raise ReleaseError("release list is not a JSON array")
    releases: List[Release] = []
    for item in items:
        if not isinstance(item, dict) or item.get("draft"):
            continue
        tag = item.get("tag_name")
        if not isinstance(tag, str) or not tag:
            continue
        assets = []
        for raw in item.get("assets") or []:
            if not isinstance(raw, dict):
                continue
            name, url, size = raw.get("name"), raw.get("browser_download_url"), raw.get("size")
            if not isinstance(name, str) or not isinstance(url, str) or not isinstance(size, int):
                continue
            try:
                safe_name(name)
                check_url(url)
            except (PackageError, ReleaseError):
                continue  # ignore assets with unsafe names or hosts; they can never be used
            assets.append(Asset(name, url, size))
        releases.append(Release(
            tag=tag,
            name=str(item.get("name") or tag),
            prerelease=bool(item.get("prerelease")),
            published_at=str(item.get("published_at") or item.get("created_at") or ""),
            body=str(item.get("body") or "")[:4000],
            assets=tuple(assets),
        ))
    releases.sort(key=lambda r: _parse_time(r.published_at), reverse=True)
    return releases


def select_release(releases: Sequence[Release], include_prereleases: bool = False) -> Release:
    """Newest non-draft release. Stable only unless include_prereleases is set."""
    if not releases:
        raise ReleaseError("the repository has no published releases")
    if include_prereleases:
        return releases[0]
    for release in releases:
        if not release.prerelease:
            return release
    raise NoStableRelease(
        "no stable release is published yet. To install the newest prerelease, run again with "
        "--include-prereleases.")


class HttpDownloader(Downloader):
    """urllib-based downloader with timeouts, redirect policy and streamed size limits."""

    def __init__(self, timeout_s: float = TIMEOUT_S) -> None:
        self._timeout = timeout_s

    def fetch(self, url: str, max_bytes: int, token: Optional[str] = None,
              accept: str = "application/octet-stream") -> bytes:
        check_url(url)
        opener = urllib.request.build_opener(_RedirectPolicy(url))
        headers = {"User-Agent": "orbit12-installer", "Accept": accept}
        if token and urllib.parse.urlsplit(url).hostname == API_HOST:
            headers["Authorization"] = f"Bearer {token}"
        request = urllib.request.Request(url, headers=headers)
        try:
            with opener.open(request, timeout=self._timeout) as response:
                declared = response.headers.get("Content-Length")
                if declared is not None and declared.isdigit() and int(declared) > max_bytes:
                    raise ReleaseError(f"download is {declared} bytes; the limit is {max_bytes}")
                chunks = []
                total = 0
                while True:
                    chunk = response.read(65536)
                    if not chunk:
                        break
                    total += len(chunk)
                    if total > max_bytes:
                        raise ReleaseError(f"download exceeds the {max_bytes}-byte limit")
                    chunks.append(chunk)
                return b"".join(chunks)
        except urllib.error.HTTPError as error:
            if error.code == 403 and error.headers.get("X-RateLimit-Remaining") == "0":
                raise ReleaseError(
                    "GitHub rate limit reached. Wait, or set GITHUB_TOKEN and try again.") from None
            if error.code == 404:
                raise ReleaseError("not found on GitHub (check --repo and the release)") from None
            raise ReleaseError(f"GitHub answered HTTP {error.code}") from None
        except urllib.error.URLError as error:
            raise ReleaseError(f"network error: {error.reason}") from None
        except TimeoutError:
            raise ReleaseError("network timeout while contacting GitHub") from None


class _RedirectPolicy(urllib.request.HTTPRedirectHandler):
    """Checks every hop and drops the Authorization header when the host changes."""

    max_redirections = MAX_REDIRECTS
    max_repeats = MAX_REDIRECTS

    def __init__(self, origin: str) -> None:
        super().__init__()
        self._origin_host = urllib.parse.urlsplit(origin).hostname

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        check_url(newurl)
        new = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new is not None and urllib.parse.urlsplit(newurl).hostname != self._origin_host:
            for key in list(new.headers):
                if key.lower() == "authorization":
                    del new.headers[key]
            for key in list(new.unredirected_hdrs):
                if key.lower() == "authorization":
                    del new.unredirected_hdrs[key]
        return new
