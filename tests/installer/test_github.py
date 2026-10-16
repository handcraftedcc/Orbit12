"""Release selection, release JSON handling and the bounded downloader (github.py)."""

import io
import json
import unittest
from unittest import mock

from helpers import FakeDownloader

from orbit12_installer import github
from orbit12_installer.github import HttpDownloader, NoStableRelease, ReleaseError


def asset(name, size=10, host="github.com"):
    return {"name": name, "browser_download_url": f"https://{host}/x/{name}", "size": size}


def release(tag, *, prerelease=False, draft=False, published="2026-10-01T10:00:00Z", assets=None):
    return {"tag_name": tag, "name": tag, "prerelease": prerelease, "draft": draft,
            "published_at": published, "body": f"notes for {tag}",
            "assets": assets if assets is not None else [asset("release-manifest.json")]}


def releases_json(*items):
    return json.dumps(list(items)).encode("utf-8")


class SelectionTests(unittest.TestCase):
    def test_stable_is_preferred_over_a_newer_prerelease(self):
        releases = github.parse_releases(releases_json(
            release("v0.2.0-pre.1", prerelease=True, published="2026-10-10T00:00:00Z"),
            release("v0.1.0", published="2026-09-01T00:00:00Z"),
        ))
        self.assertEqual(github.select_release(releases).tag, "v0.1.0")

    def test_include_prereleases_picks_the_newest_including_prereleases(self):
        releases = github.parse_releases(releases_json(
            release("v0.1.0", published="2026-09-01T00:00:00Z"),
            release("v0.2.0-pre.1", prerelease=True, published="2026-10-10T00:00:00Z"),
        ))
        self.assertEqual(github.select_release(releases, include_prereleases=True).tag, "v0.2.0-pre.1")

    def test_drafts_are_skipped_even_when_newest(self):
        releases = github.parse_releases(releases_json(
            release("v0.3.0", draft=True, published="2026-11-01T00:00:00Z"),
            release("v0.1.0", published="2026-09-01T00:00:00Z"),
        ))
        self.assertEqual([r.tag for r in releases], ["v0.1.0"])
        self.assertEqual(github.select_release(releases).tag, "v0.1.0")

    def test_no_stable_release_says_so_and_gives_the_flag_hint(self):
        releases = github.parse_releases(releases_json(
            release("v0.2.0-pre.1", prerelease=True, published="2026-10-10T00:00:00Z")))
        with self.assertRaises(NoStableRelease) as caught:
            github.select_release(releases)
        self.assertIn("--include-prereleases", str(caught.exception))
        self.assertEqual(github.select_release(releases, include_prereleases=True).tag, "v0.2.0-pre.1")

    def test_empty_list_is_an_error(self):
        with self.assertRaises(ReleaseError):
            github.select_release(github.parse_releases(b"[]"))

    def test_ordering_uses_published_date_not_list_position(self):
        releases = github.parse_releases(releases_json(
            release("v0.1.0", published="2026-09-01T00:00:00Z"),
            release("v0.2.0", published="2026-10-01T00:00:00Z"),
        ))
        self.assertEqual(releases[0].tag, "v0.2.0")

    def test_assets_with_foreign_hosts_or_unsafe_names_are_dropped(self):
        releases = github.parse_releases(releases_json(release("v0.1.0", assets=[
            asset("release-manifest.json"),
            asset("evil.bin", host="example.com"),
            asset("../escape.uf2"),
            {"name": "x.uf2", "browser_download_url": "http://github.com/x", "size": 1},
        ])))
        self.assertEqual([a.name for a in releases[0].assets], ["release-manifest.json"])

    def test_non_list_or_bad_json_is_an_error(self):
        with self.assertRaises(ReleaseError):
            github.parse_releases(b"{}")
        with self.assertRaises(ReleaseError):
            github.parse_releases(b"[")

    def test_releases_url_requires_owner_and_name(self):
        self.assertEqual(github.releases_url("handcraftedcc/Orbit12"),
                         "https://api.github.com/repos/handcraftedcc/Orbit12/releases?per_page=100")
        with self.assertRaises(ReleaseError):
            github.releases_url("handcraftedcc")


class UrlPolicyTests(unittest.TestCase):
    def test_only_https_and_allowlisted_hosts(self):
        github.check_url("https://objects.githubusercontent.com/a")
        for bad in ("http://github.com/a", "https://example.com/a", "file:///etc/passwd",
                    "https://github.com.evil.example/a"):
            with self.assertRaises(ReleaseError, msg=bad):
                github.check_url(bad)


class RedirectPolicyTests(unittest.TestCase):
    def _redirect(self, origin, target, token_header=True):
        import urllib.request

        headers = {"Authorization": "Bearer secret"} if token_header else {}
        request = urllib.request.Request(origin, headers=headers)
        policy = github._RedirectPolicy(origin)
        return policy.redirect_request(request, None, 302, "Found", {}, target)

    def test_authorization_is_dropped_when_the_host_changes(self):
        new = self._redirect("https://api.github.com/repos/x/y/releases", "https://objects.githubusercontent.com/z")
        self.assertIsNotNone(new)
        self.assertFalse(any(k.lower() == "authorization" for k in new.headers))
        self.assertFalse(any(k.lower() == "authorization" for k in new.unredirected_hdrs))

    def test_redirect_to_a_foreign_host_is_refused(self):
        with self.assertRaises(ReleaseError):
            self._redirect("https://github.com/x", "https://evil.example/payload")

    def test_redirect_downgrade_to_http_is_refused(self):
        with self.assertRaises(ReleaseError):
            self._redirect("https://github.com/x", "http://github.com/x")


class _FakeResponse(io.BytesIO):
    def __init__(self, data, headers=None):
        super().__init__(data)
        self.headers = headers or {}

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False


class _FakeOpener:
    def __init__(self, response):
        self.response = response
        self.requests = []

    def open(self, request, timeout=None):
        self.requests.append(request)
        return self.response


class DownloaderTests(unittest.TestCase):
    def _fetch(self, response, max_bytes, token=None, url="https://github.com/x/y"):
        opener = _FakeOpener(response)
        with mock.patch.object(github.urllib.request, "build_opener", return_value=opener):
            data = HttpDownloader().fetch(url, max_bytes, token=token)
        return data, opener.requests

    def test_streamed_size_limit_is_enforced_without_content_length(self):
        with self.assertRaisesRegex(ReleaseError, "limit"):
            self._fetch(_FakeResponse(b"x" * 1000), max_bytes=100)

    def test_declared_content_length_over_limit_is_refused_before_reading(self):
        with self.assertRaisesRegex(ReleaseError, "limit"):
            self._fetch(_FakeResponse(b"", {"Content-Length": "5000"}), max_bytes=100)

    def test_within_limit_returns_the_bytes(self):
        data, _ = self._fetch(_FakeResponse(b"hello"), max_bytes=100)
        self.assertEqual(data, b"hello")

    def test_token_is_sent_only_to_the_api_host(self):
        _, requests = self._fetch(_FakeResponse(b"ok"), 10, token="tok", url="https://api.github.com/repos")
        self.assertEqual(requests[0].get_header("Authorization"), "Bearer tok")
        _, requests = self._fetch(_FakeResponse(b"ok"), 10, token="tok", url="https://github.com/x/asset")
        self.assertFalse(requests[0].has_header("Authorization"))

    def test_foreign_host_is_refused_before_any_request(self):
        with self.assertRaises(ReleaseError):
            HttpDownloader().fetch("https://evil.example/x", 10)


class FakeDownloaderTests(unittest.TestCase):
    def test_fake_downloader_enforces_its_limit_like_the_real_one(self):
        fake = FakeDownloader({"https://github.com/a": b"12345"})
        with self.assertRaises(ReleaseError):
            fake.fetch("https://github.com/a", 4)


if __name__ == "__main__":
    unittest.main()
