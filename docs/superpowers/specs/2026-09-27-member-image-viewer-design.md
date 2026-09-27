# Member image viewer — design

*2026-09-27*

In the Add Member and Edit Member dialogs, clicking the photo box or the ID image box opens the file picker only when the box is empty. When it shows an image, the click opens a viewer with the image and three buttons: Change…, Remove, Close.

## The problem

`MemberEditorDialog::eventFilter` sends every click on `m_photoPreview` or `m_idImagePreview` to `choosePhotoImage()` / `chooseIdImage()`. A librarian who clicks a member's photo to look at it closer gets a file picker instead. The only way to see an ID card at a readable size is to find the file on disk. There is also no way to take an image off a member once it is set.

## Decisions

| Question | Decision |
|---|---|
| Click on an empty box | Opens the file picker, as today. |
| Click on a box whose image cannot be read | Treated as empty: opens the file picker. "Cannot be read" means `QPixmap(path).isNull()`, the same test `showImagePreview` already uses to fall back to the placeholder. |
| Click on a box showing an image | Opens `ImageViewerDialog`, modal over the member dialog. |
| Tooltip on the box | `member.choosePhoto` / `member.chooseIdImage` when empty; `member.viewPhoto` / `member.viewIdImage` when it shows an image. Updated whenever the preview is refreshed. |
| The picker button beside the form field | Unchanged: always opens the file picker. |
| Viewer title | `member.field.photo` or `member.field.idImage`, the existing field labels. |
| Viewer image size | Large, whatever the file's own size: that is the point of the viewer. The image is scaled, up or down, to the largest size that fits 80 % of the available geometry of the screen the member dialog is on, keeping aspect ratio. A 300 × 400 photo on a 1080-pixel-high screen shows about 650 × 860, not at the size of the box. Upscaling uses smooth scaling; some softness on a small source is accepted. |
| Viewer window | Opens at that size, centred on the member dialog. It can be resized or maximised, and the image rescales to fill the space left above the buttons, again keeping aspect ratio. |
| Viewer buttons | Change…, Remove, Close. Close is the default and Escape triggers it. |
| Change… | Opens the same picker the box uses (`VLMS::askForImageFile` with the slot's title and `member.imageFilter`). A picked file replaces the image in the viewer, which stays open. Cancelling the picker changes nothing. Change can be pressed again. |
| Remove | Closes the viewer. The box shows its empty placeholder and the picker button goes back to `member.choosePhoto` / `member.chooseIdImage`. No confirmation: nothing is written until the member dialog's OK, and its Cancel undoes it. |
| Close | Closes the viewer. Any file picked with Change stays picked. |
| When changes are saved | Only when the member dialog is accepted, exactly like picking a file today. |
| Remove, then pick a new file | The new file wins: the slot is "changed", not "removed". |
| Pick a new file, then Remove | The slot is "removed"; the picked file is forgotten. |
| Remove on a new member | Clears the dialog's choice only; there is nothing stored to clear. |
| Stored file after a saved removal | Deleted from `resources/members/<id>/`, after the save's transaction commits. The column is cleared inside the transaction. A failed save leaves both column and file as they were. |
| Deleting the file fails | The save still succeeds; the column is already clear and the file is only an orphan. No message. |
| Stored path is absolute | Only the column is cleared; the file is never deleted. `resolveImagePath` passes absolute paths through unchanged, and such a path points outside `resources/` (imported data), possibly at a librarian's own original. |

## Components

### `ImageViewerDialog` (new)

`applications/vlms/src/ui/ImageViewerDialog.{h,cpp}`, on `vlms_ui`. It knows nothing about members.

```cpp
class ImageViewerDialog final : public QDialog {
public:
    using FilePicker = std::function<QString()>;   // empty string = cancelled
    ImageViewerDialog(const QString& title, const QString& imagePath,
                      FilePicker pickFile, QWidget* parent = nullptr);

    enum class Outcome { Unchanged, Changed, Removed };
    [[nodiscard]] Outcome outcome() const;
    [[nodiscard]] QString imagePath() const;   // the path now shown; meaningful for Changed
};
```

- The image is a `QLabel` that keeps the original `QPixmap` and redraws a scaled copy from it in `resizeEvent`, so resizing never compounds scaling losses. The label's size policy is expanding; its initial size hint is the fitted size described above, so the window opens large.
- Change calls `pickFile()`; a non-empty result that loads as a pixmap replaces the image and sets the outcome to `Changed`. A file that does not load is ignored, as the member dialog already ignores it (the preview falls back to its placeholder).
- Remove sets the outcome to `Removed` and calls `accept()`.
- Close calls `reject()` if the outcome is still `Unchanged`, otherwise `accept()`. The member dialog reads `outcome()`, not the exec result.
- No retranslation: the viewer is modal over a modal dialog, so the language selector in the header cannot be reached while it is open. `LicenceDialog` works the same way.

### `MemberEditorDialog`

- `eventFilter`: on a release over a preview, `hasImage(slot)` decides between the picker and the viewer.
- `hasImage(slot)`: display path non-empty and `QPixmap(path)` not null. The pixmap is already loaded in `refresh…PreviewPixmap`; that function records the result in `m_photoShown` / `m_idImageShown` so the check does not reload the file.
- The picker call moves into one member, `pickImageFile(slot)`, used by the box, the button and the viewer's `FilePicker`. It goes through a static test hook:

```cpp
using ImageFilePicker = std::function<QString(QWidget*, const QString& title, const QString& filter)>;
static void setImageFilePickerForTesting(ImageFilePicker picker);   // empty = VLMS::askForImageFile
```

- Viewer outcome `Changed`: same effect as picking that file today (source path, changed flag, button text, preview).
- Viewer outcome `Removed`: clears the source and display paths, sets `m_photoRemoved` / `m_idImageRemoved`, clears the changed flag, resets the button text and the preview.
- Picking a file clears the removed flag.
- New accessors: `photoRemoved()`, `idImageRemoved()`.
- `retranslateUi` sets the box tooltips from the shown state instead of always to the "choose" string.

### `MemberWrite` and `MemberRepository`

- `MemberWrite` gains `bool clearPhoto = false;` and `bool clearIdImage = false;`. A source path and a clear flag on the same slot is a caller bug; the source path wins.
- `saveExistingMember`: for a clear flag, inside the transaction, read the stored relative path, then `UPDATE members SET photo_path = NULL` (or `id_image_path`) with `updated_at`. After the transaction returns OK, remove each file recorded that way whose stored path is relative, resolved through `resolveImagePath`. `std::filesystem::remove` with an `error_code`; errors ignored.
- `saveNewMember` ignores the clear flags.
- `MembersPage` sets the flags from `dialog.photoRemoved()` / `dialog.idImageRemoved()` in both the add and the edit path, next to the existing `photoChanged()` lines.

## Strings

New keys in all three tables, checked by `tst_strings_parity`:

| Key | en | fr | ar |
|---|---|---|---|
| `member.viewPhoto` | View photo | Voir la photo | عرض الصورة |
| `member.viewIdImage` | View ID image | Voir la pièce d'identité | عرض صورة الهوية |
| `imageViewer.change` | Change… | Changer… | تغيير… |
| `imageViewer.remove` | Remove | Retirer | إزالة |
| `imageViewer.close` | Close | Fermer | إغلاق |

## Tests

Core (`libraries/Core/test`):
- Saving an existing member with `clearPhoto` sets `photo_path` to NULL and deletes the file; `id_image_path` and its file are untouched. Same for `clearIdImage`.
- A save that fails validation with `clearPhoto` set leaves the column and the file in place.
- `clearPhoto` on a member without a photo succeeds and changes nothing.
- `clearPhoto` on a member whose `photo_path` is an absolute path clears the column and leaves that file on disk.

UI (`applications/vlms/test`), with the picker hook set to a lambda that records calls and returns a fixture image:
- Click on an empty photo box calls the picker and does not open a viewer.
- Click on a box whose path points to a non-image file calls the picker.
- Click on a box showing an image opens an `ImageViewerDialog` and does not call the picker.
- In the viewer, Change with a picked file shows the new image and keeps the viewer open; Close then leaves the member dialog with `photoChanged()` and the new path.
- Change with a cancelled pick leaves the outcome `Unchanged`.
- Remove closes the viewer; the member dialog reports `photoRemoved()`, the box shows its placeholder, and its tooltip is `member.choosePhoto`.
- Remove, then a new pick: `photoChanged()` true, `photoRemoved()` false.
- A source image smaller than the box opens larger than the box: with a 200 × 260 fixture, the shown pixmap is at least 80 % of the screen's available height or width (whichever limits it).
- A source image larger than the screen is shrunk to fit, keeping its aspect ratio.
- Resizing the viewer rescales the shown pixmap to the new space.

Modal dialogs are driven with the helpers in `applications/vlms/test/src/ModalTest.h`, as the existing dialog tests do: a zero-delay timer finds the `ImageViewerDialog` through `QApplication::activeModalWidget()` and clicks its buttons.

## Out of scope

- Zoom, pan, rotate or crop in the viewer.
- The same viewer for book covers in `BookEditorDialog`.
- Clicking the photo on the Members page preview panel.
