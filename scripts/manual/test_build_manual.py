# scripts/manual/test_build_manual.py
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(REPO / "scripts"))
import build_manual as bm  # noqa: E402


class FrontMatterTest(unittest.TestCase):
    def test_three_flat_keys(self):
        meta, body = bm.parse_front_matter("---\ntitle: A: b\norder: 3\nsummary: s\n---\n\nhi\n")
        self.assertEqual(meta, {"title": "A: b", "order": "3", "summary": "s"})
        self.assertEqual(body.strip(), "hi")


class MarkdownTest(unittest.TestCase):
    def r(self, text, lang="en"):
        return bm.render_markdown(text, lang)

    def test_heading_with_anchor(self):
        self.assertIn('<h2 id="loan-states">Loan states</h2>', self.r("## Loan states {#loan-states}"))

    def test_heading_without_anchor_is_numbered(self):
        self.assertIn('<h2 id="h-1">One</h2>', self.r("## One"))

    def test_inline(self):
        html = self.r("Click `Add Book`, **now**, *gently*, see [x](members#filters).")
        self.assertIn('<span class="ui">Add Book</span>', html)
        self.assertIn("<strong>now</strong>", html)
        self.assertIn("<em>gently</em>", html)
        self.assertIn('<a href="members.html#filters">x</a>', html)

    def test_escapes_html(self):
        self.assertIn("a &lt; b", self.r("a < b"))

    def test_shot(self):
        html = self.r("![The window](shot:gs-window)", "ar")
        self.assertIn('src="../assets/shots/ar/gs-window.png"', html)
        self.assertIn("<figcaption>The window</figcaption>", html)

    def test_lists(self):
        html = self.r("- a\n- b\n\n1. one\n2. two\n   more")
        self.assertIn("<ul><li>a</li><li>b</li></ul>", html)
        self.assertIn("<ol><li>one</li><li>two more</li></ol>", html)

    def test_steps_group_into_one_list(self):
        html = self.r("::: step\n**Do A.** Then.\n:::\n\n::: step\n**Do B.**\n:::")
        self.assertEqual(html.count('<ol class="steps">'), 1)
        self.assertEqual(html.count("<li>"), 2)

    def test_note_and_warning(self):
        self.assertIn('<aside class="note"><p>n</p></aside>', self.r("::: note\nn\n:::"))
        self.assertIn('<aside class="warning"><p>w</p></aside>', self.r("::: warning\nw\n:::"))

    def test_table(self):
        html = self.r("| a | b |\n|---|---|\n| 1 | 2 |")
        self.assertIn("<table><thead><tr><th>a</th><th>b</th></tr></thead>", html)
        self.assertIn("<tbody><tr><td>1</td><td>2</td></tr></tbody></table>", html)


class LabelsTest(unittest.TestCase):
    def test_reads_the_three_tables(self):
        labels = bm.load_ui_labels(REPO / "libraries/Core/src/Strings.cpp")
        self.assertIn("Add Book", labels["en"])
        self.assertIn("إضافة كتاب", labels["ar"])
        self.assertIn("Ajouter un ouvrage", labels["fr"])
        self.assertNotIn("Add Book", labels["fr"])


class BuildTest(unittest.TestCase):
    def test_fixture_site_builds_with_rtl_and_sidebar(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bm.copy_skeleton(REPO / "docs/manual", root)
            bm.copy_tree(HERE / "fixtures/content", root / "content")
            pages = bm.build(root)
            ar = pages[root / "ar/index.html"]
            self.assertIn('<html lang="ar" dir="rtl">', ar)
            self.assertIn('href="getting-started.html"', ar)
            self.assertIn('href="../en/index.html"', ar)  # language switcher, same page
            self.assertIn('<html lang="en" dir="ltr">', pages[root / "en/index.html"])
            self.assertIn(root / "index.html", pages)

    def test_check_reports_missing_pages_and_images(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bm.copy_skeleton(REPO / "docs/manual", root)
            bm.copy_tree(HERE / "fixtures/content", root / "content")
            problems = bm.check_site(root, bm.build(root))
            self.assertTrue(any("missing page" in p and "catalogue" in p for p in problems))
            self.assertTrue(any("missing image" in p for p in problems))


class RealSiteTest(unittest.TestCase):
    """The committed manual. Skipped until the content exists (Tasks 6-9)."""

    def test_committed_manual_is_complete(self):
        root = REPO / "docs/manual"
        if not (root / "content/ar/reference.md").exists():
            self.skipTest("manual content not written yet")
        problems = bm.check_site(root, bm.build(root))
        self.assertEqual(problems, [], "\n".join(problems))

    def test_committed_html_is_up_to_date(self):
        root = REPO / "docs/manual"
        if not (root / "ar/reference.html").exists():
            self.skipTest("manual not generated yet")
        for path, html in bm.build(root).items():
            self.assertEqual(path.read_text(encoding="utf-8"), html, f"stale: {path}")


if __name__ == "__main__":
    unittest.main()
