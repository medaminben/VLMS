# Licence dialog

**Date:** 2026-09-22
**Status:** approved, not yet implemented

Clicking the copyright line in the footer opens a modal dialog showing the application's
licence, written in whichever of the three languages the librarian is using.

## Why

The application has no licence. There is no `LICENSE` file in the repository and nothing
in the interface says on what terms the library holds the software. It was written as a
gift to the Public Library of Ksour Essef, and the terms of that gift should be readable
by the people who received it — in their own language, from inside the application, not
from a text file on a developer's machine.

The copyright line in the footer is where a reader already looks for this, so that is
where it opens from.

## The licence

A deed of gift, not a software licence in the usual sense. The Library owns the solution
outright; what remains is the gift itself, the Library's ownership of its own records, and
the three protections the author asked for.

Deliberately **not** MIT or any other open-source licence. The source is not distributed —
the library receives an installed Windows application — so a source licence would describe
rights nobody is being given.

Two clauses considered and dropped: *"for this library only"* and *"no modification"*.
Both restrict what the owner of a work may do with it, and the owner here is the Library,
so neither would have meant anything. The footer's `© {year} المكتبة العمومية بقصور الساف`
stands unchanged, and the licence now agrees with it rather than contradicting it.

| # | Clause | Substance |
|---|---|---|
| — | *Preamble* | Written for the Library and given to it as a gift. Nothing was charged, nothing is owed, and the gift cannot be taken back. |
| 1 | The solution is the Library's | The application as installed belongs to the Library — to use, keep, copy and pass on as it sees fit. |
| 2 | The Library's records are its own | Catalogue, member records, scans and photographs belong to the Library alone. The author claims no right over them and keeps no copy. |
| 3 | No warranty | Given as it is. No promise of freedom from faults, fitness for a purpose, or continued operation. The Library should keep its own backups. |
| 4 | No liability | Not responsible for loss or damage following from use, mistaken or deliberate — records lost or corrupted, work interrupted, and anything following from either. |
| 5 | No duty to support | No obligation to support, train, update or repair. Any help given continues the gift; it is not a duty owed. |
| — | *Contact* | "The author", then the name, LinkedIn and email. |

The author is referred to throughout as *the author*, never by name. The name appears once,
in the contact block, and in Arabic script in the Arabic text.

This is plain-language wording, not a lawyer-reviewed instrument. If the donation is ever
formalised with the municipality, the text should be read by someone qualified.

## Where the text lives

The three tables in `libraries/Core/src/Strings.cpp`, one key per clause part. The licence
is seven short paragraphs under six headings — the shape the table already holds — and putting it there means
the existing `test_strings_parity` guards it for nothing: a clause missing or empty in one
language fails the build.

Fifteen keys:

```
licence.title
licence.intro
licence.clause.ownership.head   licence.clause.ownership.body
licence.clause.data.head        licence.clause.data.body
licence.clause.warranty.head    licence.clause.warranty.body
licence.clause.liability.head   licence.clause.liability.body
licence.clause.support.head     licence.clause.support.body
licence.contact.head            licence.contact.author
licence.tooltip
```

The LinkedIn URL and the email address are **not** string keys. They are the same in every
language, so a key would be three identical entries, and identical entries would defeat the
"Arabic differs from Latin" check below. They are file-scope constants in
`LicenceDialog.cpp`:

```cpp
constexpr auto kLinkedIn = "https://www.linkedin.com/in/mlbh/";
constexpr auto kEmail = "mohamed.ben-hassine@mailfence.com";
```

Rejected: HTML files in a new `licence.qrc`. Prose reads better as prose and would be
easier to hand to someone for an Arabic wording check, but it is a second localisation
path parallel to `Strings`, outside the parity test, needing its own. Not worth it for
seven paragraphs.

Rejected: one `licence.body` key per language holding the whole document. A 1,500-character
string literal in a map makes unreadable diffs and turns a one-clause wording fix into a
change to a huge line.

## The dialog

`applications/vlms/src/ui/LicenceDialog.{h,cpp}` — a `QDialog` built the way
`LoanExtendDialog` is: `QVBoxLayout`, a body widget, and a `QDialogButtonBox` passed
through `localizeButtonBox`.

```cpp
class LicenceDialog final : public QDialog {
    Q_OBJECT

public:
    explicit LicenceDialog(QWidget* parent = nullptr);

    /// The rendered licence as plain text. For tests; nothing in the
    /// application reads it.
    [[nodiscard]] QString documentText() const;

private:
    void buildUi();

    QTextBrowser* m_body = nullptr;
};
```

The body is a read-only, frameless `QTextBrowser` rather than a `QLabel` in a
`QScrollArea`: the text scrolls, and a librarian can select and copy a clause.
`setOpenExternalLinks(false)` and the contact details are rendered as plain text, not
anchors — clicking a line in a licence should not launch a web browser out of a library
workstation.

Buttons: `QDialogButtonBox::Close` only. Size `560 × 620`. Window title `licence.title`.

The HTML is composed in `buildUi` from the keys, each interpolated value passed through
`toHtmlEscaped()`:

```
<div dir="…">
  <h2>title</h2>
  <p>intro</p>
  <h3>clause head</h3><p>clause body</p>     ×5
  <h3>contact head</h3>
  <p>author<br>LinkedIn<br>email</p>
</div>
```

**Language.** The dialog is modal, run with `exec()`, so the language selector in the
header is unreachable while it is open and the text can never go stale underneath it. It
therefore has no `retranslateUi`; each open composes from the current `Locale::code()`.

**Direction.** `Locale::isRtl()` drives both `m_body->setLayoutDirection(...)` and the
`dir` attribute on the wrapping `<div>` — the widget attribute aligns the viewport, the
document attribute sets block direction inside it.

## The click

`m_footerLabel` keeps its appearance exactly: same text, same muted style, no underline, no
hover change. It gains only a pointing-hand cursor and a tooltip from `licence.tooltip`,
set in `MainWindow::retranslateUi` beside the copyright text.

The click itself goes in a new widget rather than an event filter inside `MainWindow`,
because — as CLAUDE.md records and `applications/vlms/test/CMakeLists.txt` repeats —
`MainWindow` has no test: it reaches for `qobject_cast<Application*>(qApp)`, so anything
put there cannot be verified except by hand.

`applications/vlms/src/ui/ClickableLabel.{h,cpp}`:

```cpp
namespace VLMS {

/// A QLabel that reports a left-click. Only the footer's copyright line needs
/// this, but MainWindow cannot be constructed in a test, so the click lives
/// here where it can have one.
class ClickableLabel final : public QLabel {
    Q_OBJECT

public:
    explicit ClickableLabel(QWidget* parent = nullptr);

signals:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;
};

}  // namespace VLMS
```

The constructor sets `Qt::PointingHandCursor`. `mouseReleaseEvent` emits only for
`Qt::LeftButton` released inside `rect()` — a press that drags off the label before release
is not a click — then calls the base implementation.

`MainWindow::m_footerLabel` changes type from `QLabel*` to `VLMS::ClickableLabel*`,
and `buildUi` connects `clicked` to a lambda that runs `LicenceDialog(this).exec()`.

Accessibility was raised and declined: the label stays a label, so it is not reachable by
Tab and a screen reader announces it as text rather than as a button. Recorded here so the
next session knows it was a choice, not an oversight.

## Build registration

- `applications/vlms/CMakeLists.txt` — `src/ui/ClickableLabel.cpp`,
  `src/ui/LicenceDialog.cpp` and their headers, beside `src/ui/LanguageSelector.cpp`.
- `applications/vlms/test/CMakeLists.txt` — `src/test_licence_dialog.cpp`,
  `src/test_clickable_label.cpp`.
- `libraries/Core/test/CMakeLists.txt` — `src/test_licence_strings.cpp`.

## Testing

**Core — `test_licence_strings.cpp`.** The parity test already proves that whatever keys
exist exist in all three tables and are non-empty. This adds what parity cannot see:

- Each of the fifteen keys is present in `ar`, `fr` and `en`. Parity would stay green if
  all three tables lost the licence together; this would not.
- For every key, the Arabic value differs from the English. An untranslated clause left as
  English in the Arabic table is the realistic failure, and Arabic script versus Latin makes
  this a reliable check. French and English are deliberately **not** compared:
  `licence.title` is legitimately "Licence" in both.

**UI — `test_licence_dialog.cpp`.** For each of `ar`, `fr`, `en`, with `Locale::setCode`
and a `TearDown` restoring the default:

- `documentText()` contains that language's rendering of the library's name.
- `documentText()` contains every clause body for that locale, via `Strings::rawValue`.
- `documentText()` contains the LinkedIn URL and the email.
- The Close button reads `Strings::rawValue(locale, "common.close")`.
- `layoutDirection()` on the body is `Qt::RightToLeft` for `ar` and `Qt::LeftToRight` for
  the other two.
- The body is `isReadOnly()`.

**UI — `test_clickable_label.cpp`.** Emits `clicked` once on a left-button release inside
the label; does not emit on a right-button release. Driven with `QTest::mouseClick`.

**By hand.** `MainWindow` has no test, so the wiring — cursor, tooltip, and the click
actually opening the dialog — is verified by launching the application and reading the
dialog in all three languages, light and dark. A screenshot of each goes in the final
report.

## Out of scope

- No `LICENCE.md` at the repository root. The source stays private and unlicensed; this
  text governs the installed application, and duplicating it in a second place invites the
  two copies to drift.
- No keyboard access to the copyright label, per the decision above.
- No clickable links in the contact block.
- The footer's `footer.copyright` string is unchanged in all three languages.

## Appendix — the text

The wording to be entered into the three tables. Reviewed for sense, not by a lawyer.

### English

> **Licence**
>
> VLMS was written for the Public Library of Ksour Essef and is given to it as a gift.
> Nothing was charged for it, nothing is owed for it, and the gift cannot be taken back.
>
> **The solution is the Library's**
> The application, as it is installed here, belongs to the Public Library of Ksour Essef.
> The Library may use it, keep it, copy it and pass it on as it sees fit.
>
> **The Library's records are its own**
> The catalogue, the member records, the scans and the photographs the Library creates with
> this application belong to the Library alone. The author claims no right over them and
> keeps no copy of them.
>
> **No warranty**
> The application is given as it is. No promise is made that it is free of faults, that it
> suits any particular purpose, or that it will keep working. The Library should keep its
> own backups of its records.
>
> **No liability**
> The author is not responsible for any loss or damage that follows from using this
> application, whether the use was mistaken or deliberate. This includes records lost or
> corrupted, work interrupted, and anything that follows from either.
>
> **No duty to support**
> The author is under no obligation to provide support, training, updates or repairs. Any
> help given is a continuation of the gift, not a duty owed.
>
> **Contact**
> The author: Mohamed Lamine Ben Hassine

### French

> **Licence**
>
> VLMS a été écrit pour la Bibliothèque publique de Ksour Essef et lui est offert en
> don. Rien n'a été facturé, rien n'est dû, et ce don est irrévocable.
>
> **La solution appartient à la Bibliothèque**
> L'application, telle qu'elle est installée ici, appartient à la Bibliothèque publique de
> Ksour Essef. La Bibliothèque peut l'utiliser, la conserver, la copier et la transmettre
> comme elle l'entend.
>
> **Les données de la Bibliothèque lui appartiennent**
> Le catalogue, les fiches des adhérents, les numérisations et les photographies que la
> Bibliothèque crée avec cette application lui appartiennent exclusivement. L'auteur n'y
> revendique aucun droit et n'en conserve aucune copie.
>
> **Aucune garantie**
> L'application est fournie en l'état. Aucune promesse n'est faite quant à l'absence de
> défauts, à l'adéquation à un usage particulier, ou à la continuité de son fonctionnement.
> La Bibliothèque doit conserver ses propres sauvegardes.
>
> **Aucune responsabilité**
> L'auteur n'est pas responsable des pertes ou dommages résultant de l'utilisation de cette
> application, que cette utilisation ait été erronée ou délibérée. Cela comprend les données
> perdues ou corrompues, le travail interrompu, et toute conséquence de l'un ou l'autre.
>
> **Aucune obligation d'assistance**
> L'auteur n'est tenu à aucune assistance, formation, mise à jour ni réparation. Toute aide
> apportée prolonge le don ; elle n'est pas une obligation.
>
> **Contact**
> L'auteur : Mohamed Lamine Ben Hassine

### Arabic

> **الرخصة**
>
> كُتب برنامج VLMS من أجل المكتبة العمومية بقصور الساف، وهو هدية مُهداة إليها. لم
> يُطلب عنه أي مقابل، ولا شيء مستحق عليه، والهبة لا رجعة فيها.
>
> **الحلّ ملك للمكتبة**
> التطبيق، كما هو مُثبَّت هنا، ملك للمكتبة العمومية بقصور الساف. وللمكتبة أن تستعمله وتحتفظ
> به وتنسخه وتنقله كما تشاء.
>
> **بيانات المكتبة ملك لها**
> الفهرس وبطاقات المنخرطين والمسوحات والصور التي تنشئها المكتبة بهذا التطبيق ملك للمكتبة
> وحدها. ولا يدّعي المؤلف أيّ حقّ فيها ولا يحتفظ بنسخة منها.
>
> **بلا ضمان**
> يُقدَّم التطبيق كما هو. ولا يُقدَّم أيّ وعد بخلوّه من العيوب، أو بملاءمته لغرض بعينه، أو
> باستمرار عمله. وعلى المكتبة أن تحتفظ بنسخ احتياطية من بياناتها.
>
> **بلا مسؤولية**
> لا يتحمّل المؤلف مسؤولية أيّ خسارة أو ضرر ينشأ عن استعمال هذا التطبيق، سواء كان الاستعمال
> خاطئًا أو متعمَّدًا. ويشمل ذلك ضياع البيانات أو تلفها، وتعطّل العمل، وكلّ ما يترتّب على
> ذلك.
>
> **لا التزام بالدعم**
> لا يلتزم المؤلف بتقديم دعم أو تدريب أو تحديثات أو إصلاحات. وكلّ عون يُقدَّم هو امتداد
> للهبة، لا واجب مستحقّ.
>
> **جهة الاتصال**
> المؤلف: محمد الأمين بن حسين

### In every language

> https://www.linkedin.com/in/mlbh/
> mohamed.ben-hassine@mailfence.com

### Tooltip

| Locale | `licence.tooltip` |
|---|---|
| ar | عرض رخصة الاستعمال |
| fr | Afficher la licence |
| en | Show the licence |
