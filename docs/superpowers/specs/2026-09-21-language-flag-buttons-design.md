# Language flag buttons

**Date:** 2026-09-21
**Status:** approved, not yet implemented

Replace the language combo box in the header with three circular flag buttons — Tunisia,
France, the United Kingdom — sitting beside the theme toggle at the same diameter, exactly
one pressed at a time.

## Why

The combo box is the only drop-down in the header and it costs two clicks and a read to
change language. Three flags say the same thing at a glance, in a row that already holds
the sun/crescent toggle: both are "how the application is presented" rather than anything
to do with the library's records.

## What it looks like

The picked language sits in a filled disc with a ring; the other two are dimmed. This was
chosen over "grey until picked" and "ring only" because it reads fastest at 34 px.

| State | Background | Border | Icon |
|---|---|---|---|
| active | `pressedBg` | 1 px `focusBorder` | 26 px, full colour |
| inactive | transparent | transparent | 22 px, 60% opacity |
| hover (inactive) | `hoverBg` | transparent | unchanged |
| keyboard focus | as above | 2 px `focusBorder` | unchanged |

Hover deliberately does **not** brighten the icon: a stylesheet cannot reach a `QIcon`, and
repainting the pixmap on enter/leave is not worth the code.

Buttons are 34 px to match `QPushButton#themeToggle` in `Theme.cpp`. Layout order is
`ar, fr, en`; the header inherits the application direction, so Arabic mirrors the cluster
with no extra work.

## Components

Two new units, both constructible in a test without `Application` — which the combo box
never was, because `MainWindow` reaches for `qobject_cast<Application*>(qApp)`.

### `ui/FlagIcons.{h,cpp}`

Pure painting, no widgets:

```cpp
/// The flag for a UI language code ("ar", "fr", "en"), drawn rather than
/// shipped as artwork, the way themeModeIcon draws the sun and crescent.
QIcon languageFlagIcon(const QString& code, int pixelSize, qreal ratio = 1.0,
                       bool dimmed = false);
```

Lives in its own file rather than beside `themeModeIcon` in `Theme.cpp`, which is already
1163 lines.

### `ui/LanguageSelector.{h,cpp}`

A `QWidget` holding the three buttons in a `QButtonGroup` with `setExclusive(true)`, so Qt
enforces one-at-a-time and a click on the active flag cannot clear it.

```cpp
class LanguageSelector final : public QWidget {
    Q_OBJECT
public:
    explicit LanguageSelector(QWidget* parent = nullptr);
    void setCurrentLanguage(const QString& code);  // does not emit
    void retranslateUi();                          // tooltips + accessible names
signals:
    void languageSelected(const QString& code);
};
```

`retranslateUi()` also repaints the icons, because the active/dimmed split has to survive a
theme change the same way the brand banner does.

### `MainWindow`

- `m_languageCombo` and its `blockSignals` / `findData` juggling go away (~25 lines).
- `m_languageSelector` replaces it in the header layout, before the theme toggle.
- `onLanguageChanged(int index)` becomes `onLanguageChanged(const QString& code)`; the body
  still guards `code == app->uiLocale()` and calls `Application::setUiLocale`.
- `retranslateUi()` calls `m_languageSelector->setCurrentLanguage(Locale::code())` and
  `retranslateUi()` instead of walking combo items.

Persistence is unchanged: `Application::setUiLocale` already saves.

## Styling

Rules go in `Theme.cpp` next to `QPushButton#themeToggle`, following the `navLink` pattern
already there (`QPushButton#navLink[active="true"]` at line 179): each button is named
`languageButton`, carries an `active` property, and `MainWindow`-style
`refreshWidgetStyle(button)` is called when it changes. Icon size is not something a
stylesheet can set, so `LanguageSelector` calls `setIconSize` and repaints the icon itself
on every state change.

## Strings

`lang.ar` / `lang.fr` / `lang.en` stay in `Strings.cpp` and become each button's tooltip and
accessible name — the flags carry no text, so this is all a screen reader gets. Same
treatment `themeToggle` already uses.

## The artwork

Transcribed from the Wikimedia Commons SVG for each flag. All three are public domain. Each
is painted into a square and clipped to a circle, at the widget's device pixel ratio.

**France** (`Flag_of_France.svg`, 900×600) — three equal vertical thirds of the square:
`#002654`, `#ffffff`, `#CE1126`. A centre crop would show white at double width, which is
the mistake the first draft made.

**United Kingdom** (`Flag_of_the_United_Kingdom_(3-5).svg`, viewBox `0 0 50 30`) — the box
is scaled **non-uniformly** to the square, so the whole design survives instead of a cropped
middle. In viewBox units:

```
field      rect 0,0 50x30                                fill #012169
saltire    lines 0,0->50,30 and 50,0->0,30   pen 6 (white)
saltire    same lines, clipped to the counterchange path, pen 4 (#C8102E)
           clip: M25,15h25v15zv15h-25zh-25v-15zv-15h25z
cross      M-1 11h22v-12h8v12h22v8h-22v12h-8v-12h-22z
           fill #C8102E, stroke #FFF width 2
```

**Tunisia** (`Flag_of_Tunisia.svg`, viewBox `-60 -40 120 80`) — uniform scale, zoomed 1.5×
about the centre, so the white disc lands at three quarters of the circle instead of half,
leaving a red margin half its original width. A non-uniform stretch is wrong here: it would
turn the disc into an ellipse. In viewBox units, on a `#e70013` field:

```
disc       circle r=20 at origin          fill #fff
crescent   circle r=15 at origin          fill #e70013
           circle r=12 at cx=4            fill #fff
star       M-5 0l16.281-5.29L1.22 8.56V-8.56L11.28 5.29z   fill #e70013
```

At 1.5× the visible square is 53.33 viewBox units on a side, centred on the origin.

## Tests

`applications/vlms/test/src/test_language_selector.cpp`, added to the
`test_vlms_ui` target:

- picking `fr` releases `ar` — never two pressed at once
- `languageSelected` carries the code that was clicked
- clicking the already-active flag emits nothing
- `setCurrentLanguage` moves the selection without emitting
- tooltips and accessible names follow a language change
- every code in the selector has an icon (no null `QIcon` from a typo'd code)

## Out of scope

- No change to how the locale is stored or loaded.
- No fourth language.
- The combo box elsewhere (the book editor's language field) is untouched.
