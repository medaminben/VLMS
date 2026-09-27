# Birth date dropdowns — design

*2026-09-24*

The Add Member and Edit Member dialogs replace the free-text date of birth with three dropdowns: day, month, year. The value saved is still `yyyy-MM-dd`.

## The problem

The date is one `QLineEdit`. In Arabic the line runs right to left and the digits run left to right, so a date that looks right on screen is what the warning then refuses. `QDateEdit` is not a way out: a right-to-left date edit reverses its sections.

## Decisions

| Question | Decision |
|---|---|
| Shape | Three `QComboBox` dropdowns. Reading order is day, then month, then year. |
| Labels | No caption reading Day, Month, or Year, in any language. No hyphen between the dropdowns. The only label is the existing form label `member.field.dateOfBirth`. |
| Width | Compact. Day and month are only as wide as `00` plus the popup arrow. Year is only as wide as `0000` plus the popup arrow. A small gap between them. They do not stretch to fill the form cell. The Sex combo on the same row is unchanged. |
| Direction of each combo | Left to right, so a number never jumps inside its box. |
| Direction of the row | The dialog's. French and English show the day on the left. Arabic shows the day on the right. |
| Lists | Day `01`–`31`. Month `01`–`12`. Year `1916`–`2016` inclusive. Zero-padded day and month. Four-digit years. |
| Popup | Five rows and a scrollbar. `setMaxVisibleItems(5)` is not enough: Fusion plus the application stylesheet makes `QComboBox::showPopup` size the container to the screen and pin that tall list to the screen edge, so after the height is clamped the container is moved with `mapToGlobal` to sit under the combo (or above it when it does not fit), left-aligned in LTR and right-aligned in RTL. Nothing selected opens at the top. The scrollbar stays on the right even in Arabic, by keeping the popup view left to right. |
| A new member | All three unset. Do not default to `01` / `01` / `1916`. An unset combo shows empty current text. No placeholders. |
| What is saved | `yyyy-MM-dd`. The repository checks are unchanged. |
| The hint sentence | `member.field.dateOfBirthHint` goes away. |

## The row

A widget `BirthDateEdit` in `applications/vlms/src/ui/members/`, on `vlms_ui` in `applications/vlms/CMakeLists.txt`. `MemberEditorDialog` owns one and puts it where the line edit is now, on the same row as Sex.

The row is a horizontal layout in this order: day, month, year. No labels inside it. The layout follows the parent, so Arabic reverses it. Each combo is `Qt::LeftToRight`.

`BirthDateEdit` exposes the three combos, `setIsoDate`, and `isoDate`. Filling from a record does not move the focus.

`setIsoDate` splits on `-`. A part that is already in that combo's list is selected. A part that is all digits but outside the list is inserted and selected, so an edited member whose year is outside `1916`–`2016` (or whose day or month is outside its list) still shows the stored value and a save does not silently change it. Anything that is not a digit part, including `not a date`, leaves that combo unset.

`isoDate` is `yyyy-MM-dd` when all three are selected. Any unset combo makes `isoDate` an empty string.

## The warning

The OK button still uses `firstValidationFailure` and the same three keys. The focus target is one of the three combos.

| Combos | Key | Focus |
|---|---|---|
| Any combo unset | `member.dateOfBirthRequired` | The first unset combo, day then month then year |
| All three set, not a real calendar date | `member.dateOfBirthInvalid` | Day |
| A real date after today | `member.dateOfBirthInFuture` | Year |

`memberInput().dateOfBirth` is `isoDate()`. A date the warning accepts is always `yyyy-MM-dd`. With a maximum year of 2016 the future-date case is rare; the check stays.

## Verification

- Day items are `01`–`31`, month `01`–`12`, year `1916`–`2016`. `maxVisibleItems` is 5.
- A new dialog has all three unset and fails with `member.dateOfBirthRequired` on the day combo.
- Selecting `31`, `02`, and a non-leap year fails with `member.dateOfBirthInvalid` on the day combo.
- Selecting `12`, `05`, `1990` produces `memberInput` date of birth `1990-05-12` and no failure.
- Loading a member whose year is outside `1916`–`2016` keeps that year selected.
- Under a right-to-left parent, the day combo is to the right of the year combo.
- The placeholder assertion on `member.field.dateOfBirthHint` is removed with the key, from all three string tables.

## Out of scope

- The loan, extend, and return date edits. They stay on `QDateEdit`.
- Retaking the manual screenshots.
- Folding Arabic-Indic digits into ASCII.
- A custom item delegate. Coloured popup entries are not required.
