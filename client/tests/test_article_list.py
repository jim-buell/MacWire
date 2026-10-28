"""Run the actual client list, sync, and body-cache code with host Toolbox shims.

Only platform allocation, transport, and drawing calls are replaced. This tests
multi-packet list assembly and failure behavior beyond the portable wire decoder.
"""
import ctypes as c
from dataclasses import replace
from datetime import datetime, timedelta, timezone
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "backend"))
from models import ArticleIndexEntry, ArticleContent
from protocol import encode_refresh, encode_index_page, encode_dates, encode_article, encode_feeds, local_timezone


class ArticleListTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        client = ROOT / "client"
        support = client / "tests/host_toolbox"
        library = Path(cls.temp.name) / "articles.so"
        sources = [client / name for name in ("Articles.c", "Protocol.c", "Wire.c", "Network.c", "Feeds.c",
                                              "ConnectionConfig.c", "DemoTransport.c", "ReadState.c", "ReadHistory.c", "ListPosition.c")]
        subprocess.run(["cc", "-std=c99", "-Wall", "-Werror=implicit-function-declaration",
                        "-shared", "-fPIC", "-DRSS_COMPAT_H", "-I"+str(support), "-I"+str(client),
                        "-include", str(support/"support.h"), *map(str, sources), str(support/"support.c"), str(support/"files.c"),
                        "-o", str(library)], check=True)
        cls.lib = c.CDLL(str(library))
        cls.lib.ParsePageResponse.argtypes = [c.c_void_p, c.c_ulong, c.c_void_p, c.POINTER(c.c_ulong),
                                             c.POINTER(c.c_ulong), c.POINTER(c.c_short)]
        cls.lib.ParsePageResponse.restype = c.c_short
        for function in ("ParseDatesResponse", "ParseArticleResponse"):
            getattr(cls.lib, function).argtypes = [c.c_void_p, c.c_ulong]
            getattr(cls.lib, function).restype = c.c_short
        for function in ("TestArticleID", "TestArticleDate", "TestArticleTime", "TestArticleAuthor", "GetCachedArticleBody"):
            getattr(cls.lib, function).argtypes = [c.c_short]
            getattr(cls.lib, function).restype = c.c_char_p
        cls.lib.AllocateArticleList.argtypes = [c.c_ulong]
        cls.lib.AllocateArticleList.restype = c.c_ubyte
        cls.lib.SetArticleBody.argtypes = [c.c_short, c.c_char_p]
        cls.lib.SetArticleBody.restype = c.c_ubyte
        cls.lib.ReadHistorySet.argtypes = [c.c_char_p, c.c_double, c.c_int]
        cls.lib.ReadHistoryObserve.argtypes = [c.c_double]
        cls.lib.SetArticleVisibility.argtypes = [c.c_ushort]
        cls.lib.FindArticleByID.argtypes = [c.c_char_p]
        cls.lib.FindArticleByID.restype = c.c_short
        cls.lib.GetStatusMessage.argtypes = [c.c_void_p]
        cls.lib.TestSetPrefsDirectory.argtypes = [c.c_char_p]
        cls.lib.ReadHistoryError.restype = c.c_char_p
        cls.lib.ReadHistoryContains.argtypes = [c.c_char_p]
        cls.lib.TestQueuePacket.argtypes = [c.c_char_p, c.c_void_p, c.c_ulong, c.c_int]
        cls.lib.TestArticleLinkURL.argtypes = [c.c_short,c.c_short]
        cls.lib.TestArticleLinkURL.restype = c.c_char_p
        cls.lib.TestArticleLinkCount.argtypes = [c.c_short]
        cls.lib.TestArticleLinkCount.restype = c.c_short

    @classmethod
    def tearDownClass(cls):
        cls.lib.AllocateArticleList(0)
        cls.lib.TestResetPackets()
        cls.temp.cleanup()

    def setUp(self):
        c.c_int.in_dll(self.lib, "testFailAllocation").value = 0
        self.lib.AllocateArticleList(0)
        self.lib.TestResetPackets()
        self.prefs = Path(tempfile.mkdtemp(dir=self.temp.name))
        self.lib.TestSetPrefsDirectory(str(self.prefs).encode())
        self.lib.InitReadHistory()
        self.lib.CancelListPositionRestore()
        c.c_short.in_dll(self.lib, "gFeedCount").value = 0
        c.c_int.in_dll(self.lib, "gLastSyncSucceeded").value = 0
        (c.c_char * 33).in_dll(self.lib, "gFeedFilter").value = b""
        c.c_ubyte.in_dll(self.lib, "gNetworkUnavailable").value = 0
        c.c_ubyte.in_dll(self.lib, "gNewestFirst").value = 1
        c.c_ubyte.in_dll(self.lib, "gUsingStartupSamples").value = 0
        self.view = c.create_string_buffer(9)
        self.total, self.offset, self.count = c.c_ulong(), c.c_ulong(), c.c_short()
        now = datetime(2026, 10, 3, 17, tzinfo=timezone.utc)
        self.entries = [ArticleIndexEntry(id=f"{i:08x}", feed="Busy", title=f"Story {i}", summary="",
                                          published=now+timedelta(minutes=i), has_full_content=True)
                        for i in range(125)]

    def append(self, offset, token="1234abcd", total=None, count=None):
        entries = self.entries[offset:offset+(count or 50)]
        packet = encode_index_page(entries, token, len(self.entries) if total is None else total, offset)
        return self.lib.ParsePageResponse(packet, len(packet), self.view, c.byref(self.total),
                                          c.byref(self.offset), c.byref(self.count))

    def load(self):
        for offset in (0, 50, 100):
            self.assertEqual(self.append(offset), 0)
            dates = encode_dates(self.entries[offset:offset+50], local_timezone("America/Chicago"), compact=True)
            self.assertEqual(self.lib.ParseDatesResponse(dates, len(dates)), 0)

    def ids(self):
        count = c.c_short.in_dll(self.lib, "gMaxArticles").value
        return [self.lib.TestArticleID(i).decode() for i in range(count)]

    def test_formatting_only_body_metadata_is_cached_and_evicted(self):
        from models import ArticleContent
        from protocol import encode_article
        self.load()
        self.lib.TestArticleStyleCount.argtypes=[c.c_short]
        self.lib.TestArticleStyleCount.restype=c.c_short
        packet=encode_article(ArticleContent(id='00000000',full_text='Bold',styles=[{'start':0,'length':4,'flags':1}]))
        self.assertEqual(self.lib.ParseArticleResponse(packet,len(packet)),0)
        self.assertEqual(self.lib.TestArticleStyleCount(0),1)
        self.assertTrue(self.lib.SetArticleBody(0,b'Replacement'))
        self.assertEqual(self.lib.TestArticleStyleCount(0),0)
        self.assertEqual(self.lib.ParseArticleResponse(packet,len(packet)),0)
        c.c_short.in_dll(self.lib,"gSelectedIndex").value=-1
        for i in range(1,9): self.assertTrue(self.lib.SetArticleBody(i,b"Another body"))
        self.assertEqual(self.lib.TestArticleStyleCount(0),0)

    def test_assembles_all_pages_and_preserves_each_pages_local_dates(self):
        self.load()
        self.assertEqual(self.ids(), [entry.id for entry in self.entries])
        self.assertEqual(self.total.value, 125)
        for index in (0, 49, 50, 99, 100, 124):
            self.assertIn(b"Oct 3,", self.lib.TestArticleTime(index))

            self.assertIn(b"PM", self.lib.TestArticleTime(index))
            self.assertIn(b"2026", self.lib.TestArticleDate(index))
        date = self.lib.TestArticleDate(0)
        article = ArticleContent(id=self.entries[0].id, title="Full title", feed="Busy", full_text="Full body",
                                 published=self.entries[0].published)
        packet = encode_article(article)
        self.assertEqual(self.lib.ParseArticleResponse(packet, len(packet)), 0)
        self.assertEqual(self.lib.TestArticleDate(0), date)

    def test_link_metadata_lives_and_expires_with_its_body_cache(self):
        self.load()
        source=ArticleContent(id=self.entries[0].id,full_text='Linked words.',links=[{'start':0,'length':6,'url':'https://example.com/story'}])
        packet=encode_article(source)
        self.assertEqual(self.lib.ParseArticleResponse(packet,len(packet)),0)
        self.assertEqual(self.lib.TestArticleLinkCount(0),1)
        self.assertEqual(self.lib.TestArticleLinkURL(0,0),b'https://example.com/story')
        self.assertEqual(self.lib.GetCachedArticleBody(0),b'Linked words.')
        self.assertTrue(self.lib.SetArticleBody(0,b'Replacement.'))
        self.assertEqual(self.lib.TestArticleLinkCount(0),0)
        self.assertEqual(self.lib.ParseArticleResponse(packet,len(packet)),0)
        c.c_short.in_dll(self.lib,'gSelectedIndex').value=-1
        for i in range(1,9): self.assertTrue(self.lib.SetArticleBody(i,b'Another body.'))
        self.assertEqual(self.lib.TestArticleLinkCount(0),0)
        self.assertIsNone(self.lib.TestArticleLinkURL(0,0))

    def save_position(self, selected=23, top=21, feed=b"", newest=False):
        self.load()
        c.c_ubyte.in_dll(self.lib, "gNewestFirst").value = newest
        self.lib.SortArticles()
        c.c_short.in_dll(self.lib, "gSelectedIndex").value = selected
        c.c_short.in_dll(self.lib, "gScrollOffset").value = top
        (c.c_char * 33).in_dll(self.lib, "gFeedFilter").value = feed
        c.c_int.in_dll(self.lib, "gLastSyncSucceeded").value = 1
        self.assertEqual(self.lib.SaveListPosition(), 1)

    def reload_position(self):
        self.lib.AllocateArticleList(0)
        self.view.value = b""
        c.c_ubyte.in_dll(self.lib, "gNewestFirst").value = 1
        (c.c_char * 33).in_dll(self.lib, "gFeedFilter").value = b""
        self.lib.LoadListPosition()

    def test_position_survives_new_arrivals_by_article_id(self):
        self.save_position(feed=b"Busy")
        self.reload_position()
        self.assertEqual(c.c_ubyte.in_dll(self.lib, "gNewestFirst").value, 0)
        self.assertEqual((c.c_char * 33).in_dll(self.lib, "gFeedFilter").value, b"Busy")
        earlier = replace(self.entries[0], id="abcdabcd", published=self.entries[0].published-timedelta(minutes=1))
        self.entries.insert(0, earlier)
        self.load()
        self.lib.SetArticleBody(24, b"Cached body")
        self.assertEqual(self.lib.RestoreListPosition(), 1)
        self.assertEqual(c.c_short.in_dll(self.lib, "gSelectedIndex").value, 24)
        self.assertEqual(c.c_short.in_dll(self.lib, "gScrollOffset").value, 22)
        self.assertEqual(self.lib.RestoreListPosition(), 0)

    def test_expired_position_falls_back_to_nearest_publication(self):
        self.save_position(selected=0, top=0)
        self.reload_position()
        self.entries = self.entries[10:]
        self.load()
        self.lib.SetArticleBody(0, b"Cached body")
        self.assertEqual(self.lib.RestoreListPosition(), 1)
        self.assertEqual(self.lib.TestArticleID(c.c_short.in_dll(self.lib, "gSelectedIndex").value), b"0000000a")
        self.assertEqual(c.c_short.in_dll(self.lib, "gScrollOffset").value, 0)

    def test_saved_blank_reader_stays_blank(self):
        self.save_position(selected=-1, top=15)
        self.reload_position()
        self.load()
        self.assertEqual(self.lib.RestoreListPosition(), 1)
        self.assertEqual(c.c_short.in_dll(self.lib, "gSelectedIndex").value, -1)
        self.assertEqual(c.c_short.in_dll(self.lib, "gScrollOffset").value, 15)

    def test_restore_keeps_selected_title_visible_when_saved_top_is_elsewhere(self):
        for top, expected in ((0, 20), (70, 23)):
            with self.subTest(saved_top=top):
                self.view.value = b""
                self.save_position(selected=23, top=top)
                self.reload_position()
                self.load()
                self.lib.SetArticleBody(23, b"Cached body")
                self.assertEqual(self.lib.RestoreListPosition(), 1)
                selected = c.c_short.in_dll(self.lib, "gSelectedIndex").value
                restored_top = c.c_short.in_dll(self.lib, "gScrollOffset").value
                self.assertEqual(selected, 23)
                self.assertEqual(restored_top, expected)
                self.assertTrue(restored_top <= selected < restored_top + 4)

    def test_new_stories_between_top_and_selection_keep_selected_title_visible(self):
        self.save_position(selected=23, top=21)
        self.reload_position()
        for i in range(5):
            self.entries.insert(22+i, replace(self.entries[21], id=f"abcd000{i}",
                published=self.entries[21].published+timedelta(seconds=10+i)))
        # The fixture now needs three full batches to deliver all 130 rows.
        self.load()
        self.lib.SortArticles()
        self.lib.SetArticleBody(28, b"Cached body")
        self.assertEqual(self.lib.RestoreListPosition(), 1)
        self.assertEqual(c.c_short.in_dll(self.lib, "gSelectedIndex").value, 28)
        self.assertEqual(c.c_short.in_dll(self.lib, "gScrollOffset").value, 25)

    def test_smaller_list_viewport_keeps_restored_title_fully_visible(self):
        self.save_position(selected=23, top=21)
        self.reload_position()
        self.load()
        self.lib.SetArticleBody(23, b"Cached body")
        c.c_int.in_dll(self.lib, "testVisibleListRows").value = 2
        self.assertEqual(self.lib.RestoreListPosition(), 1)
        self.assertEqual(c.c_short.in_dll(self.lib, "gSelectedIndex").value, 23)
        self.assertEqual(c.c_short.in_dll(self.lib, "gScrollOffset").value, 22)

    def test_failed_startup_does_not_overwrite_position(self):
        self.save_position()
        before = (self.prefs/"MacWire Position").read_bytes()
        self.reload_position()
        c.c_int.in_dll(self.lib, "gLastSyncSucceeded").value = 0
        self.assertEqual(self.lib.SaveListPosition(), 0)
        self.assertEqual((self.prefs/"MacWire Position").read_bytes(), before)

    def test_short_write_and_failed_rename_preserve_saved_position(self):
        self.save_position()
        path = self.prefs/"MacWire Position"
        before = path.read_bytes()
        for variable in ("testFailWrite", "testFailRename"):
            c.c_int.in_dll(self.lib, "testRenameCount").value = 0
            c.c_int.in_dll(self.lib, variable).value = 1
            self.assertEqual(self.lib.SaveListPosition(), 0)
            self.assertEqual(path.read_bytes(), before)
            c.c_int.in_dll(self.lib, variable).value = 0

    def test_corrupt_position_uses_backup_or_default(self):
        self.save_position()
        path = self.prefs/"MacWire Position"
        (self.prefs/"MacWire Position Backup").write_bytes(path.read_bytes())
        path.write_bytes(b"broken")
        self.reload_position()
        self.assertEqual(c.c_ubyte.in_dll(self.lib, "gNewestFirst").value, 0)
        (self.prefs/"MacWire Position Backup").unlink()
        self.reload_position()
        self.assertEqual(c.c_ubyte.in_dll(self.lib, "gNewestFirst").value, 1)
        self.assertEqual(self.lib.RestoreListPosition(), 0)

    def test_removed_feed_returns_to_all_feeds(self):
        self.save_position(feed=b"Removed")
        self.reload_position()
        c.c_short.in_dll(self.lib, "gFeedCount").value = 0
        self.lib.ValidateSavedFeed()
        self.assertEqual((c.c_char * 33).in_dll(self.lib, "gFeedFilter").value, b"")

    def test_startup_sync_restores_position_once(self):
        self.save_position(selected=23, top=21)
        self.reload_position()
        self.queue_view(body_entries=self.entries[23:27])
        self.lib.SyncWithServer()
        self.assertEqual(c.c_short.in_dll(self.lib, "gSelectedIndex").value, 23)
        self.assertEqual(c.c_short.in_dll(self.lib, "gScrollOffset").value, 21)
        self.assertEqual(self.lib.TestPacketsRemaining(), 0)
        self.queue_view(body_entries=self.entries[:4])
        self.lib.SyncWithServer()
        self.assertEqual(c.c_short.in_dll(self.lib, "gSelectedIndex").value, 0)
        self.assertEqual(self.lib.TestPacketsRemaining(), 0)

    def test_sort_reverses_chronology_and_keeps_selection_body_and_date(self):
        self.load()
        selected = c.c_short.in_dll(self.lib, "gSelectedIndex")
        selected.value = 23
        self.assertTrue(self.lib.SetArticleBody(23, b"Selected body"))
        date = self.lib.TestArticleDate(23)
        self.lib.SortArticles()
        self.assertEqual(self.ids(), [entry.id for entry in reversed(self.entries)])
        self.assertEqual(self.lib.TestArticleID(selected.value), self.entries[23].id.encode())
        self.assertEqual(self.lib.GetCachedArticleBody(selected.value), b"Selected body")
        self.assertEqual(self.lib.TestArticleDate(selected.value), date)
        self.lib.ToggleArticleSort()
        self.assertEqual(self.ids(), [entry.id for entry in self.entries])
        self.assertEqual(selected.value, 23)
        self.assertEqual(self.lib.GetCachedArticleBody(23), b"Selected body")
        self.lib.ToggleArticleSort()
        self.assertEqual(self.ids(), [entry.id for entry in reversed(self.entries)])

    def test_same_timestamp_sort_is_deterministic(self):
        for entry in self.entries:
            entry.published = self.entries[0].published
        self.load()
        self.lib.SortArticles()
        self.assertEqual(self.ids(), [entry.id for entry in reversed(self.entries)])
        self.lib.ToggleArticleSort()
        self.assertEqual(self.ids(), [entry.id for entry in self.entries])

    def test_wrong_snapshot_duplicate_and_skipped_pages_leave_list_unchanged(self):
        self.assertEqual(self.append(0), 0)
        first = self.ids()
        for offset, token in ((50, "deadbeef"), (0, "1234abcd"), (100, "1234abcd")):
            self.assertEqual(self.append(offset, token), -1)
            self.assertEqual(self.ids(), first)
        self.assertEqual(self.append(50), 0)

    def test_allocation_failure_keeps_existing_view_and_reports_failure(self):
        self.load()
        previous = self.ids()
        self.view.value = b""
        c.c_int.in_dll(self.lib, "testFailAllocation").value = 1
        try:
            self.assertEqual(self.append(0), -2)
            self.assertEqual(self.ids(), previous)
            self.assertEqual(self.view.value, b"")
        finally:
            c.c_int.in_dll(self.lib, "testFailAllocation").value = 0

    def test_body_cache_stays_bounded_without_evicting_selected_article(self):
        self.load()
        c.c_short.in_dll(self.lib, "gSelectedIndex").value = 0
        for index in range(30):
            self.assertTrue(self.lib.SetArticleBody(index, f"Body {index}".encode()))
        cached = [i for i in range(125) if self.lib.GetCachedArticleBody(i)]
        self.assertEqual(len(cached), 8)
        self.assertIn(0, cached)
        self.assertIn(29, cached)
        self.assertEqual(len(self.ids()), 125)

    def queue(self, path, packet=b"", error=0):
        self.lib.TestQueuePacket(path.encode(), packet, len(packet), error)

    def queue_view(self, fail_offset=None, body_entries=None):
        self.queue("/feeds", encode_feeds(["Busy"]))
        for offset in (0, 50, 100):
            view = "" if offset == 0 else "1234abcd"
            path = f"/page?last_sync=0&feed=&hours=24&view={view}&offset={offset}"
            if offset == fail_offset:
                self.queue(path, error=-30002)
                return
            self.queue(path, encode_index_page(self.entries[offset:offset+50], "1234abcd", 125, offset))
            self.queue(f"/dates?view=1234abcd&offset={offset}",
                       encode_dates(self.entries[offset:offset+50], local_timezone("America/Chicago"), compact=True))
        for entry in list(reversed(self.entries[-4:]) if body_entries is None else body_entries)[:1]:
            self.queue("/article/"+entry.id, encode_article(ArticleContent(
                id=entry.id, title=entry.title, feed=entry.feed, published=entry.published, full_text="Body")))

    def test_manual_refresh_polls_until_finished_before_fetching_new_view(self):
        self.queue('/refresh', encode_refresh('ab123456', True))
        self.queue('/refresh?job=ab123456', encode_refresh('ab123456', True))
        self.queue('/refresh?job=ab123456', encode_refresh('ab123456', False))
        self.queue_view()
        self.lib.RefreshFeedsFromServer()
        self.assertEqual(c.c_int.in_dll(self.lib, 'gLastSyncSucceeded').value, 1)
        self.assertEqual(self.ids(), [entry.id for entry in reversed(self.entries)])
        self.assertEqual(self.lib.TestPacketsRemaining(), 0)

    def test_manual_refresh_partial_failure_still_loads_articles(self):
        self.queue('/refresh', encode_refresh('ab123456', False, 2))
        self.queue_view()
        self.lib.RefreshFeedsFromServer()
        self.assertEqual(c.c_int.in_dll(self.lib, 'gLastSyncSucceeded').value, 1)
        status = c.create_string_buffer(256)
        self.lib.GetStatusMessage(status)
        self.assertIn(b'Some feeds failed', status.raw)

    def test_manual_refresh_cancel_preserves_current_articles(self):
        self.load()
        before = self.ids()
        self.queue('/refresh', encode_refresh('ab123456', True))
        c.c_int.in_dll(self.lib, 'testCancelPoll').value = 1
        try:
            self.lib.RefreshFeedsFromServer()
            self.assertEqual(self.ids(), before)
            self.assertEqual(c.c_int.in_dll(self.lib, 'gLastSyncSucceeded').value, 0)
        finally:
            c.c_int.in_dll(self.lib, 'testCancelPoll').value = 0

    def test_author_survives_article_decode_and_sort_and_can_be_absent(self):
        self.load()
        packet = encode_article(ArticleContent(id=self.entries[0].id, author='Jane Doe', full_text='Body'))
        self.assertEqual(self.lib.ParseArticleResponse(packet, len(packet)), 0)
        self.assertEqual(self.lib.TestArticleAuthor(0), b'Jane Doe')
        self.lib.SortArticles()
        index = self.lib.FindArticleByID(self.entries[0].id.encode())
        self.assertEqual(self.lib.TestArticleAuthor(index), b'Jane Doe')
        packet = encode_article(ArticleContent(id=self.entries[0].id, full_text='Body'))
        self.assertEqual(self.lib.ParseArticleResponse(packet, len(packet)), 0)
        self.assertEqual(self.lib.TestArticleAuthor(index), b'')

    def test_arrow_cancels_photo_without_marking_rest_failed_or_showing_connection_error(self):
        self.load()
        selected = c.c_short.in_dll(self.lib, 'gSelectedIndex')
        selected.value = 0
        self.lib.SetArticleBody(0, b'Current body')
        self.lib.SetArticleBody(1, b'Next cached body')
        self.lib.TestSetImageCount(0, 3)
        self.queue('/image/' + self.entries[0].id + '/0?width=160')
        c.c_int.in_dll(self.lib, 'testNavigateOnRequest').value = 1
        self.lib.SelectArticle(0)
        self.assertEqual(selected.value, 0)
        self.assertEqual(self.lib.ArticleNavigationPending(), 1)
        self.assertEqual(self.lib.TestPacketsRemaining(), 0)
        self.assertEqual(c.c_ubyte.in_dll(self.lib, 'gNetworkUnavailable').value, 0)
        for image in range(3):
            self.assertEqual(self.lib.TestImageState(0, image), 0)
        self.lib.ApplyPendingArticleNavigation()
        self.assertEqual(selected.value, 1)
        self.assertEqual(self.lib.GetCachedArticleBody(1), b'Next cached body')
        self.assertEqual(self.lib.NetworkRequestFailed(), 0)

    def test_click_cancels_photo_and_opens_the_exact_cached_target_after_unwinding(self):
        self.load()
        selected = c.c_short.in_dll(self.lib, 'gSelectedIndex')
        selected.value = 0
        self.lib.SetArticleBody(0, b'Current body')
        self.lib.SetArticleBody(7, b'Clicked cached body')
        self.lib.TestSetImageCount(0, 3)
        self.queue('/image/' + self.entries[0].id + '/0?width=160')
        c.c_int.in_dll(self.lib, 'testSelectOnRequest').value = 8
        self.lib.SelectArticle(0)
        self.assertEqual(selected.value, 0)
        self.assertEqual(self.lib.ArticleNavigationPending(), 1)
        self.assertEqual(self.lib.TestPacketsRemaining(), 0)
        for image in range(3):
            self.assertEqual(self.lib.TestImageState(0, image), 0)
        self.lib.ApplyPendingArticleNavigation()
        self.assertEqual(selected.value, 7)
        self.assertEqual(self.lib.GetCachedArticleBody(7), b'Clicked cached body')
        self.assertEqual(self.lib.NetworkRequestFailed(), 0)
        self.assertEqual(c.c_ubyte.in_dll(self.lib, 'gNetworkUnavailable').value, 0)

    def test_click_replaces_queued_arrow_target_and_following_arrow_advances_from_it(self):
        self.load()
        selected = c.c_short.in_dll(self.lib, 'gSelectedIndex')
        selected.value = 0
        self.lib.SetArticleBody(8, b'Final target')
        self.assertEqual(self.lib.QueueArticleNavigation(1), 1)
        self.assertEqual(self.lib.QueueArticleSelection(7), 1)
        self.assertEqual(c.c_short.in_dll(self.lib,'testPreviewSelection').value,7)
        self.assertEqual(selected.value,0)
        self.assertEqual(self.lib.QueueArticleSelection(125), 0)
        self.assertEqual(self.lib.QueueArticleNavigation(1), 1)
        self.lib.ApplyPendingArticleNavigation()
        self.assertEqual(selected.value, 8)
        self.assertEqual(self.lib.QueueArticleSelection(8), 0)
        self.assertEqual(self.lib.ArticleNavigationPending(), 0)

    def test_idle_prefetch_loads_one_body_at_a_time_without_changing_selection(self):
        self.load()
        selected = c.c_short.in_dll(self.lib, 'gSelectedIndex')
        selected.value = 0
        self.lib.SetArticleBody(0, b'Current body')
        self.lib.SetArticleBody(3, b'Already cached')
        self.queue('/article/'+self.entries[0].id,encode_article(ArticleContent(
            id=self.entries[0].id,full_text='Current body')))
        self.lib.FetchArticle(self.entries[0].id.encode())
        self.lib.TestAdvanceTicks(5000)
        for index in (1,2):
            self.queue('/article/'+self.entries[index].id, encode_article(ArticleContent(
                id=self.entries[index].id,full_text='Prefetched body')))
        self.lib.PrefetchIdleArticle()
        self.assertEqual(self.lib.TestPacketsRemaining(),1)
        self.assertEqual(selected.value,0)
        self.assertEqual(self.lib.GetCachedArticleBody(1),b'Prefetched body')
        self.lib.TestAdvanceTicks(20)
        self.lib.PrefetchIdleArticle()
        self.assertEqual(self.lib.TestPacketsRemaining(),0)
        self.lib.TestAdvanceTicks(20)
        self.lib.PrefetchIdleArticle()
        self.assertEqual(self.lib.NetworkRequestFailed(),0)
        self.assertEqual(selected.value,0)

    def test_idle_work_finishes_selected_body_before_prefetching_neighbors(self):
        self.load()
        c.c_short.in_dll(self.lib,'gSelectedIndex').value=0
        self.lib.TestAdvanceTicks(5000)
        self.queue('/article/'+self.entries[0].id,encode_article(ArticleContent(
            id=self.entries[0].id,full_text='Selected body')))
        self.queue('/article/'+self.entries[1].id,encode_article(ArticleContent(
            id=self.entries[1].id,full_text='Neighbor body')))
        self.lib.PrefetchIdleArticle()
        self.assertEqual(self.lib.GetCachedArticleBody(0),b'Selected body')
        self.assertEqual(self.lib.GetCachedArticleBody(1),None)
        self.assertEqual(self.lib.TestPacketsRemaining(),1)

    def test_idle_work_finishes_selected_images_before_prefetching_neighbors(self):
        self.load()
        c.c_short.in_dll(self.lib,'gSelectedIndex').value=0
        self.lib.SetArticleBody(0,b'Selected body')
        self.lib.TestSetImageCount(0,2)
        self.lib.TestAdvanceTicks(5000)
        for image in (0,1):
            self.queue('/image/'+self.entries[0].id+'/'+str(image)+'?width=160')
        self.queue('/article/'+self.entries[1].id,encode_article(ArticleContent(
            id=self.entries[1].id,full_text='Neighbor body')))
        self.lib.PrefetchIdleArticle()
        self.assertEqual(self.lib.TestPacketsRemaining(),2)
        self.lib.TestAdvanceTicks(20)
        self.lib.PrefetchIdleArticle()
        self.assertEqual(self.lib.TestPacketsRemaining(),1)
        self.assertEqual(self.lib.GetCachedArticleBody(1),None)

    def test_neighbor_failure_backoff_does_not_delay_selected_images(self):
        self.load()
        c.c_short.in_dll(self.lib,'gSelectedIndex').value=0
        self.lib.SetArticleBody(0,b'Selected body')
        self.lib.TestAdvanceTicks(5000)
        self.queue('/article/'+self.entries[1].id,error=-30002)
        self.lib.PrefetchIdleArticle()
        self.lib.TestSetImageCount(0,1)
        self.queue('/image/'+self.entries[0].id+'/0?width=160')
        self.lib.PrefetchIdleArticle()
        self.assertEqual(self.lib.TestPacketsRemaining(),0)

    def test_idle_prefetch_failure_is_quiet_and_throttled(self):
        self.load()
        c.c_short.in_dll(self.lib,'gSelectedIndex').value=0
        self.lib.SetArticleBody(0,b'Current body')
        self.queue('/article/'+self.entries[0].id,encode_article(ArticleContent(
            id=self.entries[0].id,full_text='Current body')))
        self.lib.FetchArticle(self.entries[0].id.encode())
        self.lib.TestAdvanceTicks(5000)
        self.queue('/article/'+self.entries[1].id,error=-30002)
        self.lib.PrefetchIdleArticle()
        self.assertEqual(self.lib.TestPacketsRemaining(),0)
        self.assertEqual(self.lib.NetworkRequestFailed(),0)
        self.assertEqual(c.c_ubyte.in_dll(self.lib,'gNetworkUnavailable').value,0)
        self.queue('/article/'+self.entries[1].id,encode_article(ArticleContent(
            id=self.entries[1].id,full_text='After retry')))
        self.lib.PrefetchIdleArticle()
        self.assertEqual(self.lib.TestPacketsRemaining(),1)
        self.lib.TestAdvanceTicks(4000)
        self.lib.PrefetchIdleArticle()
        self.assertEqual(self.lib.TestPacketsRemaining(),0)

    def test_deferred_command_stops_new_requests_and_photo_sequence(self):
        self.load()
        c.c_short.in_dll(self.lib,'gSelectedIndex').value=0
        self.lib.SetArticleBody(0,b'Current body')
        self.lib.TestSetImageCount(0,3)
        c.c_int.in_dll(self.lib,'testDeferredAction').value=1
        self.lib.SelectArticle(0)
        self.assertEqual(self.lib.TestPacketsRemaining(),0)
        self.assertEqual(c.c_ubyte.in_dll(self.lib,'gNetworkUnavailable').value,0)
        for image in range(3):
            self.assertEqual(self.lib.TestImageState(0,image),0)
        c.c_int.in_dll(self.lib,'testDeferredAction').value=0
        self.lib.ClearCancelledRequest()
        self.assertEqual(self.lib.NetworkRequestFailed(),0)

    def test_navigation_interrupts_prefetch_and_defers_new_network_request(self):
        self.load()
        c.c_short.in_dll(self.lib, 'gSelectedIndex').value = 0
        self.lib.SetArticleBody(0, b'Current body')
        self.queue('/article/' + self.entries[1].id)
        c.c_int.in_dll(self.lib, 'testNavigateOnRequest').value = 1
        self.lib.PrefetchArticles(0, 3)
        self.assertEqual(self.lib.TestPacketsRemaining(), 0)
        self.assertEqual(self.lib.ArticleNavigationPending(), 1)
        self.lib.SetArticleBody(1, b'Next cached body')
        self.lib.ApplyPendingArticleNavigation()
        self.assertEqual(c.c_short.in_dll(self.lib, 'gSelectedIndex').value, 1)
        self.assertEqual(self.lib.NetworkRequestFailed(), 0)

    def test_feeds_menu_populates_before_index_and_article_downloads(self):
        self.queue_view()
        self.lib.SyncWithServer()
        self.assertEqual(c.c_int.in_dll(self.lib, 'testFeedsAtFirstPage').value, 1)
        self.assertEqual(c.c_int.in_dll(self.lib, 'testFeedsAtFirstArticle').value, 1)

    def test_cached_sources_populate_menu_before_article_when_feeds_endpoint_fails(self):
        # Simulate an older Helper without /feeds.
        self.queue('/feeds', error=-30002)
        for offset in (0, 50, 100):
            view = '' if offset == 0 else '1234abcd'
            self.queue(f'/page?last_sync=0&feed=&hours=24&view={view}&offset={offset}',
                       encode_index_page(self.entries[offset:offset+50], '1234abcd', 125, offset))
            self.queue(f'/dates?view=1234abcd&offset={offset}',
                       encode_dates(self.entries[offset:offset+50], local_timezone('America/Chicago'), compact=True))
        self.queue('/article/'+self.entries[-1].id,
                   encode_article(ArticleContent(id=self.entries[-1].id, full_text='Body')))
        self.lib.SyncWithServer()
        self.assertEqual(c.c_int.in_dll(self.lib, 'testFeedsAtFirstArticle').value, 1)

    def test_feed_checkmark_changes_before_network_and_rolls_back_on_failure(self):
        names = ((c.c_char * 33) * 64).in_dll(self.lib, 'gFeedNames')
        names[0].value = b'Busy'
        c.c_short.in_dll(self.lib, 'gFeedCount').value = 1
        self.lib.UpdateFeedsMenu()
        self.queue('/feeds', encode_feeds(['Busy']))
        self.queue('/page?last_sync=0&feed=42757379&hours=24&view=&offset=0', error=-30002)
        self.lib.SelectFeed(5)
        self.assertEqual(c.c_int.in_dll(self.lib, 'testFeedCheckedAtFirstRequest').value, 5)
        self.assertEqual(self.lib.TestFeedCheckedItem(), 3)
        self.assertEqual((c.c_char * 33).in_dll(self.lib, 'gFeedFilter').value, b'')

    def test_named_feed_checkmark_stays_selected_after_success(self):
        names = ((c.c_char * 33) * 64).in_dll(self.lib, 'gFeedNames')
        names[0].value = b'Busy'
        c.c_short.in_dll(self.lib, 'gFeedCount').value = 1
        self.lib.UpdateFeedsMenu()
        self.queue('/feeds', encode_feeds(['Busy']))
        self.queue('/page?last_sync=0&feed=42757379&hours=24&view=&offset=0',
                   encode_index_page([], '1234abcd', 0, 0))
        self.queue('/dates?view=1234abcd&offset=0', encode_dates([], local_timezone('America/Chicago'), compact=True))
        self.lib.SelectFeed(5)
        self.assertEqual(c.c_int.in_dll(self.lib, 'testFeedCheckedAtFirstRequest').value, 5)
        self.assertEqual(self.lib.TestFeedCheckedItem(), 5)
        self.assertEqual(c.c_int.in_dll(self.lib, 'gLastSyncSucceeded').value, 1)

    def test_all_feeds_checkmark_changes_before_successful_network_request(self):
        (c.c_char * 33).in_dll(self.lib, 'gFeedFilter').value = b'Busy'
        self.lib.UpdateFeedsMenu()
        self.queue('/feeds', encode_feeds(['Busy']))
        self.queue('/page?last_sync=0&feed=&hours=24&view=&offset=0',
                   encode_index_page([], '1234abcd', 0, 0))
        self.queue('/dates?view=1234abcd&offset=0', encode_dates([], local_timezone('America/Chicago'), compact=True))
        self.lib.SelectFeed(3)
        self.assertEqual(c.c_int.in_dll(self.lib, 'testFeedCheckedAtFirstRequest').value, 3)
        self.assertEqual(self.lib.TestFeedCheckedItem(), 3)
        self.assertEqual(c.c_int.in_dll(self.lib, 'gLastSyncSucceeded').value, 1)

    def test_actual_sync_fetches_entire_window_and_selects_newest(self):
        c.c_int.in_dll(self.lib, "testExpectedListCount").value = 0
        self.queue_view()
        self.lib.SyncWithServer()
        self.assertEqual(c.c_int.in_dll(self.lib, "gLastSyncSucceeded").value, 1)
        self.assertEqual(self.ids(), [entry.id for entry in reversed(self.entries)])
        self.assertEqual(c.c_short.in_dll(self.lib, "gSelectedIndex").value, 0)
        self.assertEqual(self.lib.TestPacketsRemaining(), 0)
        self.assertEqual(self.lib.GetCachedArticleBody(0), b"Body")
        for counter in ("testPartialLists", "testUnsortedListPaints", "testMissingWait", "testUnexpectedWait", "testWaitDepth"):
            self.assertEqual(c.c_int.in_dll(self.lib, counter).value, 0, counter)

    def test_sync_retains_previous_list_and_body_while_loading(self):
        self.load()
        self.lib.SortArticles()
        c.c_int.in_dll(self.lib, "testUnsortedListPaints").value = 0
        self.lib.SetArticleBody(0, b"Old reader body")
        c.c_int.in_dll(self.lib, "testExpectedListCount").value = 125
        self.queue_view()
        self.lib.SyncWithServer()
        for counter in ("testPartialLists", "testUnsortedListPaints", "testMissingWait", "testUnexpectedWait", "testWaitDepth"):
            self.assertEqual(c.c_int.in_dll(self.lib, counter).value, 0, counter)

    def test_failed_download_keeps_previous_complete_list_and_reader(self):
        self.load()
        self.lib.SortArticles()
        c.c_int.in_dll(self.lib, "testUnsortedListPaints").value = 0
        c.c_short.in_dll(self.lib, "gSelectedIndex").value = 4
        c.c_short.in_dll(self.lib, "gScrollOffset").value = 3
        self.lib.SetArticleBody(4, b"Old reader body")
        old_ids = self.ids()
        c.c_int.in_dll(self.lib, "testExpectedListCount").value = 125
        self.queue_view(fail_offset=50)
        self.lib.SyncWithServer()
        self.assertEqual(self.ids(), old_ids)
        self.assertEqual(self.lib.GetCachedArticleBody(4), b"Old reader body")
        self.assertEqual(c.c_short.in_dll(self.lib, "gSelectedIndex").value, 4)
        self.assertEqual(c.c_short.in_dll(self.lib, "gScrollOffset").value, 3)
        for counter in ("testPartialLists", "testUnsortedListPaints", "testMissingWait", "testUnexpectedWait", "testWaitDepth"):
            self.assertEqual(c.c_int.in_dll(self.lib, counter).value, 0, counter)

    def test_refresh_only_uses_wait_cursor_until_initial_list_is_available(self):
        self.queue('/refresh', encode_refresh('ab123456', False))
        self.queue_view()
        self.lib.RefreshFeedsFromServer()
        self.assertEqual(c.c_int.in_dll(self.lib, "testMaxWaitDepth").value, 1)
        self.assertEqual(c.c_int.in_dll(self.lib, "testWaitDepth").value, 0)
        self.queue('/refresh', encode_refresh('ab123456', True))
        c.c_int.in_dll(self.lib, 'testCancelPoll').value = 1
        try:
            self.lib.RefreshFeedsFromServer()
            self.assertEqual(c.c_int.in_dll(self.lib, "testWaitDepth").value, 0)
        finally:
            c.c_int.in_dll(self.lib, 'testCancelPoll').value = 0

    def test_refresh_with_existing_articles_never_shows_wait_cursor(self):
        self.load()
        self.queue('/refresh', encode_refresh('ab123456', False))
        self.queue_view()
        self.lib.RefreshFeedsFromServer()
        self.assertEqual(c.c_int.in_dll(self.lib, "testMaxWaitDepth").value, 0)
        self.assertEqual(c.c_int.in_dll(self.lib, "testUnexpectedWait").value, 0)
        self.assertEqual(c.c_int.in_dll(self.lib, "testWaitDepth").value, 0)

    def test_actual_sync_rejects_partial_view_after_transport_failure(self):
        self.queue_view(fail_offset=50)
        self.lib.SyncWithServer()
        self.assertEqual(c.c_int.in_dll(self.lib, "gLastSyncSucceeded").value, 0)
        self.assertEqual(c.c_ubyte.in_dll(self.lib, "gNetworkUnavailable").value, 1)
        self.assertEqual(self.ids(), [])
        self.assertEqual(self.lib.TestPacketsRemaining(), 0)

    def test_actual_sync_accepts_an_empty_time_window(self):
        self.queue("/feeds", encode_feeds(["Busy"]))
        self.queue("/page?last_sync=0&feed=&hours=24&view=&offset=0",
                   encode_index_page([], "1234abcd", 0, 0))
        self.queue("/dates?view=1234abcd&offset=0", encode_dates([], compact=True))
        self.lib.SyncWithServer()
        self.assertEqual(c.c_int.in_dll(self.lib, "gLastSyncSucceeded").value, 1)
        self.assertEqual(self.ids(), [])
        self.assertEqual(c.c_short.in_dll(self.lib, "gSelectedIndex").value, -1)
        self.assertEqual(self.lib.TestPacketsRemaining(), 0)

    def test_actual_sync_rejects_incomplete_local_dates(self):
        self.queue("/feeds", encode_feeds(["Busy"]))
        self.queue("/page?last_sync=0&feed=&hours=24&view=&offset=0",
                   encode_index_page(self.entries[:50], "1234abcd", 125, 0))
        dates = encode_dates(self.entries[:50], local_timezone("America/Chicago"), compact=True)
        self.queue("/dates?view=1234abcd&offset=0", dates[:-1])
        self.lib.SyncWithServer()
        self.assertEqual(c.c_int.in_dll(self.lib, "gLastSyncSucceeded").value, 0)
        self.assertEqual(self.ids(), [])
        self.assertEqual(self.lib.TestPacketsRemaining(), 0)

    def remember(self, index, read=True):
        entry = self.entries[index]
        self.lib.ReadHistorySet(entry.id.encode(), entry.published.timestamp(), read)

    def test_read_and_unread_changes_survive_reloading_from_disk(self):
        self.load()
        selected = c.c_short.in_dll(self.lib, "gSelectedIndex")
        selected.value = 23
        self.lib.ToggleMarkReadSelected()
        self.assertEqual(self.lib.ReadHistoryContains(self.entries[23].id.encode()), 1)
        self.lib.InitReadHistory()
        self.assertEqual(self.lib.ReadHistoryContains(self.entries[23].id.encode()), 1)
        self.assertEqual(self.lib.ReadHistoryContains(self.entries[22].id.encode()), 0)
        self.lib.AllocateArticleList(0)
        self.view.value = b""
        self.load()
        selected.value = 23
        self.lib.ToggleMarkReadSelected()
        self.lib.InitReadHistory()
        self.assertEqual(self.lib.ReadHistoryContains(self.entries[23].id.encode()), 0)
        self.assertEqual((self.prefs/"MacWire Reads").stat().st_size, 20)

    def test_visibility_filters_loaded_rows_without_refetching(self):
        self.remember(1); self.remember(3)
        self.load()
        c.c_short.in_dll(self.lib, "gSelectedIndex").value = 3
        self.lib.SetArticleBody(3, b"Read article body")
        self.lib.SetArticleVisibility(1)
        self.assertEqual(len(self.ids()), 123)
        self.assertNotIn(self.entries[1].id, self.ids())
        self.assertNotIn(self.entries[3].id, self.ids())
        self.assertEqual(c.c_short.in_dll(self.lib, "gSelectedIndex").value, -1)
        self.assertEqual(c.c_short.in_dll(self.lib, "gLoadedArticles").value, 125)
        self.lib.SetArticleVisibility(2)
        self.assertEqual(self.ids(), [self.entries[3].id, self.entries[1].id])
        self.assertEqual(self.lib.GetCachedArticleBody(0), b"Read article body")
        self.lib.SetArticleVisibility(0)
        self.assertEqual(len(self.ids()), 125)
        self.assertEqual(self.lib.TestPacketsRemaining(), 0)

    def test_visibility_choice_survives_reopening_and_hidden_unread_rows_can_be_restored(self):
        self.remember(1)
        self.load()
        self.lib.SetArticleVisibility(2)
        self.lib.AllocateArticleList(0)
        self.lib.InitReadHistory()
        self.assertEqual(c.c_ushort.in_dll(self.lib, "gArticleVisibility").value, 2)
        self.view.value = b""
        self.load(); self.lib.SortArticles()
        self.assertEqual(self.ids(), [self.entries[1].id])
        self.lib.SetArticleVisibility(1)
        self.assertEqual(len(self.ids()), 124)
        self.lib.SetArticleVisibility(0)
        self.assertEqual(len(self.ids()), 125)

    def test_leaving_an_unread_article_hides_it_and_keeps_the_next_selection(self):
        self.load()
        self.lib.SetArticleVisibility(1)
        selected = c.c_short.in_dll(self.lib, "gSelectedIndex")
        selected.value = 0
        self.lib.SetArticleBody(0, b"Current body")
        self.lib.SetArticleBody(1, b"Next body")
        current, next_id = self.ids()[:2]
        self.lib.SelectNextArticle()
        self.assertNotIn(current, self.ids())
        self.assertEqual(len(self.ids()), 124)
        self.assertEqual(self.lib.TestArticleID(selected.value).decode(), next_id)
        self.assertEqual(self.lib.GetCachedArticleBody(selected.value), b"Next body")
        self.assertEqual(self.lib.ReadHistoryContains(current.encode()), 1)
        self.assertEqual(self.lib.ReadHistoryContains(next_id.encode()), 0)
        self.assertEqual(self.lib.TestPacketsRemaining(), 0)

    def test_explicit_mark_read_hides_selected_row_and_status_counts_hidden_articles(self):
        self.load(); self.lib.SetArticleVisibility(1)
        c.c_short.in_dll(self.lib, "gSelectedIndex").value = 0
        current = self.ids()[0]
        self.lib.ToggleMarkReadSelected()
        self.assertNotIn(current, self.ids())
        self.assertEqual(c.c_short.in_dll(self.lib, "gSelectedIndex").value, -1)
        status = c.create_string_buffer(256)
        self.lib.GetStatusMessage(status)
        self.assertEqual(status.raw[1:1+status.raw[0]], b"124/125, 124 unread")
        self.lib.SetArticleVisibility(0)
        self.assertEqual(len(self.ids()), 125)

    def test_mark_all_read_empties_unread_view_but_keeps_all_rows_and_persists(self):
        self.load(); self.lib.SetArticleVisibility(1)
        self.lib.MarkAllArticlesRead()
        self.assertEqual(self.ids(), [])
        self.assertEqual(c.c_short.in_dll(self.lib, "gLoadedArticles").value, 125)
        self.lib.SetArticleVisibility(2)
        self.assertEqual(len(self.ids()), 125)
        self.lib.InitReadHistory()
        for entry in self.entries:
            self.assertEqual(self.lib.ReadHistoryContains(entry.id.encode()), 1)
        self.assertEqual((self.prefs/"MacWire Reads").stat().st_size, 20+125*12)

    def test_finishing_selected_article_saves_the_last_article_of_a_session(self):
        self.load()
        selected = c.c_short.in_dll(self.lib, "gSelectedIndex")
        selected.value = 0
        self.lib.SetArticleBody(0, b"Read body")
        self.lib.FinishReadingSelected()
        self.lib.InitReadHistory()
        self.assertEqual(self.lib.ReadHistoryContains(self.entries[0].id.encode()), 1)

    def test_samples_do_not_pollute_persistent_read_history(self):
        self.load()
        c.c_ubyte.in_dll(self.lib, "gUsingStartupSamples").value = 1
        c.c_short.in_dll(self.lib, "gSelectedIndex").value = 0
        self.lib.ToggleMarkReadSelected()
        self.assertEqual(self.lib.ReadHistoryContains(self.entries[0].id.encode()), 0)
        self.lib.InitReadHistory()
        self.assertEqual(self.lib.ReadHistoryContains(self.entries[0].id.encode()), 0)

    def test_failed_short_write_keeps_previous_saved_history(self):
        self.remember(1)
        self.assertEqual(self.lib.SaveReadHistory(), 1)
        original = (self.prefs/"MacWire Reads").read_bytes()
        self.remember(2)
        c.c_int.in_dll(self.lib, "testFailWrite").value = 1
        self.assertEqual(self.lib.SaveReadHistory(), 0)
        self.assertIsNotNone(self.lib.ReadHistoryError())
        self.assertEqual((self.prefs/"MacWire Reads").read_bytes(), original)
        c.c_int.in_dll(self.lib, "testFailWrite").value = 0
        self.lib.InitReadHistory()
        self.assertEqual(self.lib.ReadHistoryContains(self.entries[1].id.encode()), 1)
        self.assertEqual(self.lib.ReadHistoryContains(self.entries[2].id.encode()), 0)

    def test_failed_replacement_rename_restores_previous_file_and_can_retry(self):
        self.remember(1); self.assertEqual(self.lib.SaveReadHistory(), 1)
        original = (self.prefs/"MacWire Reads").read_bytes()
        self.remember(2)
        c.c_int.in_dll(self.lib, "testRenameCount").value = 0
        c.c_int.in_dll(self.lib, "testFailRename").value = 2
        self.assertEqual(self.lib.SaveReadHistory(), 0)
        self.assertEqual((self.prefs/"MacWire Reads").read_bytes(), original)
        c.c_int.in_dll(self.lib, "testFailRename").value = 0
        self.assertEqual(self.lib.SaveReadHistory(), 1)
        self.lib.InitReadHistory()
        self.assertEqual(self.lib.ReadHistoryContains(self.entries[1].id.encode()), 1)
        self.assertEqual(self.lib.ReadHistoryContains(self.entries[2].id.encode()), 1)

    def test_interrupted_save_recovers_backup_without_overwriting_it_early(self):
        self.remember(1); self.assertEqual(self.lib.SaveReadHistory(), 1)
        main = self.prefs/"MacWire Reads"
        old = main.read_bytes()
        self.remember(2); self.assertEqual(self.lib.SaveReadHistory(), 1)
        pending = main.read_bytes()
        main.unlink()
        (self.prefs/"MacWire Reads Backup").write_bytes(old)
        (self.prefs/"MacWire Reads Temp").write_bytes(pending)
        self.lib.InitReadHistory()
        self.assertEqual(self.lib.ReadHistoryContains(self.entries[1].id.encode()), 1)
        self.assertEqual(self.lib.ReadHistoryContains(self.entries[2].id.encode()), 0)
        self.assertEqual(self.lib.SaveReadHistory(), 1)
        self.assertEqual(main.read_bytes(), old)
        self.assertFalse((self.prefs/"MacWire Reads Backup").exists())

    def test_corrupt_history_is_reported_and_preserved(self):
        self.remember(1); self.assertEqual(self.lib.SaveReadHistory(), 1)
        main = self.prefs/"MacWire Reads"
        damaged = bytearray(main.read_bytes()); damaged[-1] ^= 1
        main.write_bytes(damaged)
        self.lib.InitReadHistory()
        self.assertIsNotNone(self.lib.ReadHistoryError())
        self.remember(2)
        self.assertEqual(self.lib.SaveReadHistory(), 0)
        self.assertEqual(main.read_bytes(), damaged)

    def test_read_history_has_no_1024_id_cap_and_prunes_with_feed_publication_time(self):
        latest = self.entries[0].published.timestamp()
        for index in range(2000):
            self.lib.ReadHistorySet(f"{index:08x}".encode(), latest, 1)
        self.assertEqual(self.lib.SaveReadHistory(), 1)
        self.assertEqual((self.prefs/"MacWire Reads").stat().st_size, 20+2000*12)
        self.lib.InitReadHistory()
        self.assertEqual(self.lib.ReadHistoryContains(b"00000000"), 1)
        self.assertEqual(self.lib.ReadHistoryContains(b"000007cf"), 1)
        self.lib.ReadHistoryObserve(latest+7*86400)
        self.assertEqual(self.lib.ReadHistoryContains(b"00000000"), 1)
        self.lib.ReadHistoryObserve(latest+7*86400+1)
        self.assertEqual(self.lib.ReadHistoryContains(b"00000000"), 0)
        self.assertEqual(self.lib.SaveReadHistory(), 1)
        self.assertEqual((self.prefs/"MacWire Reads").stat().st_size, 20)

    def test_saved_read_only_filter_applies_after_every_sync_page_arrives(self):
        self.remember(1)
        self.lib.SetArticleVisibility(2)
        self.queue_view(body_entries=[self.entries[1]])
        self.lib.SyncWithServer()
        self.assertEqual(c.c_int.in_dll(self.lib, "gLastSyncSucceeded").value, 1)
        self.assertEqual(self.ids(), [self.entries[1].id])
        self.assertEqual(c.c_short.in_dll(self.lib, "gLoadedArticles").value, 125)
        self.assertEqual(self.lib.TestPacketsRemaining(), 0)
        self.lib.SetArticleVisibility(0)
        self.assertEqual(len(self.ids()), 125)

    def test_invalid_history_headers_records_and_truncations_leave_file_untouched(self):
        self.remember(1); self.remember(2); self.assertEqual(self.lib.SaveReadHistory(), 1)
        main = self.prefs/"MacWire Reads"
        valid = main.read_bytes()
        variants = []
        for index, value in ((0, ord("X")), (5, 2), (7, 3), (15, 9), (20, ord("z"))):
            damaged = bytearray(valid); damaged[index] = value; variants.append(damaged)
        damaged = bytearray(valid); damaged[32:40] = damaged[20:28]; variants.append(damaged)
        damaged = bytearray(valid); damaged[28:32] = (0xffffffff).to_bytes(4, "big"); variants.append(damaged)
        # Recompute integrity fields so record validation is tested separately.
        for damaged in variants:
            checksum = 2166136261
            for index, byte in enumerate(damaged):
                if index < 16 or index >= 20:
                    checksum = ((checksum ^ byte)*16777619) & 0xffffffff
            damaged[16:20] = checksum.to_bytes(4, "big")
        variants.extend(valid[:length] for length in (0, 19, 20, len(valid)-1))
        for damaged in variants:
            with self.subTest(data=bytes(damaged)):
                main.write_bytes(damaged)
                self.lib.InitReadHistory()
                self.assertIsNotNone(self.lib.ReadHistoryError())
                self.assertEqual(self.lib.SaveReadHistory(), 0)
                self.assertEqual(main.read_bytes(), damaged)

    def test_history_memory_failure_does_not_overwrite_saved_file(self):
        self.remember(1); self.assertEqual(self.lib.SaveReadHistory(), 1)
        main = self.prefs/"MacWire Reads"; original = main.read_bytes()
        c.c_int.in_dll(self.lib, "testFailAllocation").value = 1
        try:
            self.lib.InitReadHistory()
            self.assertIsNotNone(self.lib.ReadHistoryError())
            self.assertEqual(self.lib.SaveReadHistory(), 0)
        finally:
            c.c_int.in_dll(self.lib, "testFailAllocation").value = 0
        self.assertEqual(main.read_bytes(), original)
        self.lib.InitReadHistory()
        self.assertEqual(self.lib.ReadHistoryContains(self.entries[1].id.encode()), 1)

    def test_page_decoder_rejects_truncation_and_inconsistent_totals(self):
        packet = encode_index_page(self.entries[:2], "1234abcd", 125, 0)
        for length in range(len(packet)):
            self.assertEqual(self.lib.ParsePageResponse(packet, length, self.view, c.byref(self.total),
                                                       c.byref(self.offset), c.byref(self.count)), -1)
        invalid = bytearray(packet)
        invalid[11:15] = (1).to_bytes(4, "big")
        data = bytes(invalid)
        self.assertEqual(self.lib.ParsePageResponse(data, len(data), self.view, c.byref(self.total),
                                                   c.byref(self.offset), c.byref(self.count)), -1)
        self.assertEqual(self.ids(), [])

    def test_selecting_visible_cached_article_does_not_repaint_whole_list(self):
        self.load()
        c.c_short.in_dll(self.lib, 'gSelectedIndex').value = 0
        c.c_short.in_dll(self.lib, 'gScrollOffset').value = 0
        self.lib.SetArticleBody(0, b'Previous body')
        self.lib.SetArticleBody(1, b'Next body')
        updates = c.c_int.in_dll(self.lib, 'testListUpdates')
        dots = c.c_int.in_dll(self.lib, 'testIndicatorUpdates')
        updates.value = dots.value = 0
        self.lib.SelectArticle(1)
        self.assertEqual(updates.value, 0)
        self.assertEqual(dots.value, 1)
        self.assertEqual(c.c_short.in_dll(self.lib, 'testUpdatedRow').value, 0)
        self.lib.EnsureArticleVisible(7)
        self.assertEqual(updates.value, 1)

    def test_prefetched_metadata_changes_only_invalidate_the_changed_row(self):
        self.load()
        c.c_short.in_dll(self.lib, 'gSelectedIndex').value = 0
        updates = c.c_int.in_dll(self.lib, 'testListUpdates')
        rows = c.c_int.in_dll(self.lib, 'testRowUpdates')
        updates.value = rows.value = 0
        packet = encode_article(ArticleContent(id=self.entries[1].id, title='Updated story',
                                               feed='Busy', full_text='New body'))
        self.assertEqual(self.lib.ParseArticleResponse(packet, len(packet)), 0)
        self.assertEqual(updates.value, 0)
        self.assertEqual(rows.value, 1)
        self.assertEqual(c.c_short.in_dll(self.lib, 'testUpdatedRow').value, 1)
        self.assertEqual(self.lib.ParseArticleResponse(packet, len(packet)), 0)
        self.assertEqual(rows.value, 1)


    def test_first_sync_invalidates_empty_list_before_fast_selection(self):
        self.queue_view()
        self.lib.SyncWithServer()
        self.assertGreater(c.c_short.in_dll(self.lib, 'gMaxArticles').value, 0)
        self.assertEqual(c.c_int.in_dll(self.lib, 'testEmptyPreview').value, 0)

    def test_direct_article_load_keeps_normal_cursor_and_publishes_text_before_photos(self):
        self.load()
        c.c_short.in_dll(self.lib, 'gSelectedIndex').value = 0
        self.queue('/article/'+self.entries[1].id, encode_article(ArticleContent(
            id=self.entries[1].id, title='New article', feed='Busy', full_text='Ready text',
            images=[{'url':'https://example.com/photo'}])))
        self.queue('/image/'+self.entries[1].id+'/0?width=160')
        self.lib.SelectArticle(1)
        self.assertEqual(self.lib.GetCachedArticleBody(1), b'Ready text')
        self.assertEqual(self.lib.TestPacketsRemaining(), 0)
        self.assertEqual(c.c_int.in_dll(self.lib, 'testUnexpectedWait').value, 0)
        self.assertEqual(c.c_int.in_dll(self.lib, 'testMaxWaitDepth').value, 0)
        self.assertEqual(c.c_int.in_dll(self.lib, 'testPhotoBeforeTextReady').value, 0)
        self.assertEqual(c.c_int.in_dll(self.lib, 'testWaitDepth').value, 0)

    def test_failed_article_load_keeps_normal_cursor_and_ends_loading_feedback(self):
        self.load()
        c.c_short.in_dll(self.lib, 'gSelectedIndex').value = 0
        self.queue('/article/'+self.entries[1].id, error=3)
        self.lib.SelectArticle(1)
        self.assertEqual(c.c_int.in_dll(self.lib, 'testWaitDepth').value, 0)
        self.assertGreater(c.c_int.in_dll(self.lib, 'testLoadingFinished').value, 0)
        self.assertEqual(c.c_int.in_dll(self.lib, 'testUnexpectedWait').value, 0)
        self.assertEqual(c.c_int.in_dll(self.lib, 'testMaxWaitDepth').value, 0)
        self.assertEqual(c.c_ubyte.in_dll(self.lib, 'gNetworkUnavailable').value, 1)

    def test_failed_article_does_not_start_photos_or_clear_the_article_error(self):
        from models import ImageData
        from protocol import encode_image
        for response, error in ((b'', 3), (b'not an article', 0), (b'', 0)):
            with self.subTest(response=response, error=error):
                self.lib.TestResetPackets()
                self.view.value = b''
                self.load()
                c.c_short.in_dll(self.lib, 'gSelectedIndex').value = 0
                self.lib.TestSetImageCount(1, 1)
                self.queue('/article/'+self.entries[1].id, response, error=error)
                self.queue('/image/'+self.entries[1].id+'/0?width=160',
                           encode_image(ImageData(8, 1, 1, b'\x00\xff'), self.entries[1].id, 0))
                self.lib.SelectArticle(1)
                self.assertEqual(self.lib.TestPacketsRemaining(), 1)
                self.assertIsNone(self.lib.GetCachedArticleBody(1))
                self.assertEqual(self.lib.TestImageState(1, 0), 0)
                self.assertEqual(self.lib.NetworkRequestFailed(), 1)
                self.assertGreater(c.c_int.in_dll(self.lib, 'testLoadingFinished').value, 0)


if __name__ == "__main__":
    unittest.main()
