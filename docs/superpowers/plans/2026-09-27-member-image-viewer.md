# Member Image Viewer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In the Add/Edit Member dialog, a click on a photo or ID image box that shows an image opens a large viewer with Change…, Remove and Close; a click on an empty box still opens the file picker.

**Architecture:** A new, member-agnostic `ImageViewerDialog` shows one image scaled to fit 80 % of the screen and reports an outcome (Unchanged / Changed / Removed). `MemberEditorDialog` decides between picker and viewer and applies the outcome to its own state. A removal travels to the database as `MemberWrite::clearPhoto` / `clearIdImage`; `MemberRepository::saveExistingMember` clears the column inside its transaction and deletes the stored file only after the commit.

**Tech Stack:** C++20, Qt 6 Widgets, SQLite through `VLMS::SqliteSession`, GoogleTest + QtTest, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-27-member-image-viewer-design.md`

## Global Constraints

- The project is **not** a git repository. There are no commit steps; each task ends with its tests passing.
- Build directory is `build/`. Build with `cmake --build build -j$(nproc)`. Run tests with `ctest --test-dir build -R '<regex>' --output-on-failure` (CTest sets `QT_QPA_PLATFORM=offscreen` and the other test environment variables).
- Every user-visible string goes through the string table in `libraries/Core/src/Strings.cpp`, in all three languages (ar, fr, en). `tst_strings_parity` fails if a key is missing from one table.
- Viewer image size: scaled up or down to the largest size that fits 80 % of the available geometry of the screen the member dialog is on, keeping aspect ratio.
- Nothing is written to the database or disk until the member dialog is accepted.
- A stored image file is deleted only when its stored path is relative (inside `resources/`), and only after the save's transaction succeeded.
- No zoom, pan, rotate or crop controls. No changes to `BookEditorDialog` or the Members page preview.
- Match the surrounding code: 4-space indent, `m_` members, `QStringLiteral`, `VLMS::T("key")` for strings, comments only where they explain a why.

---

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `libraries/Core/include/VLMS/Core/MemberTypes.h` | Modify | `MemberWrite` gains `clearPhoto`, `clearIdImage`. |
| `libraries/Core/include/VLMS/Core/MemberRepository.h` | Modify | Private `clearMemberImage`. |
| `libraries/Core/src/MemberRepository.cpp` | Modify | Clear column in transaction; delete file after commit. |
| `libraries/Core/test/src/test_member_repository.cpp` | Modify | Repository tests for clearing. |
| `libraries/Core/src/Strings.cpp` | Modify | 5 new keys × 3 languages. |
| `applications/vlms/src/ui/ImageViewerDialog.{h,cpp}` | Create | The viewer. |
| `applications/vlms/CMakeLists.txt` | Modify | Add the viewer to `vlms_ui`. |
| `applications/vlms/test/src/test_image_viewer_dialog.cpp` | Create | Viewer tests. |
| `applications/vlms/src/ui/members/MemberEditorDialog.{h,cpp}` | Modify | Picker-or-viewer click, removal state, picker test hook. |
| `applications/vlms/src/ui/members/MembersPage.cpp` | Modify | Pass removal flags into `MemberWrite`. |
| `applications/vlms/test/src/test_member_image_viewer.cpp` | Create | Member dialog tests. |
| `applications/vlms/test/CMakeLists.txt` | Modify | Register the two new UI test files. |

---

### Task 1: Clearing a member image in the repository

**Files:**
- Modify: `libraries/Core/include/VLMS/Core/MemberTypes.h:80-84`
- Modify: `libraries/Core/include/VLMS/Core/MemberRepository.h` (private section, after `storeMemberImage`)
- Modify: `libraries/Core/src/MemberRepository.cpp:644-663` (`saveExistingMember`) and after `storeMemberImage` (~line 903)
- Test: `libraries/Core/test/src/test_member_repository.cpp` (append after `SaveNewMemberRollsBackWhenPhotoFails`, ~line 967)

**Interfaces:**
- Consumes: nothing new.
- Produces: `MemberWrite::clearPhoto` and `MemberWrite::clearIdImage` (`bool`, default `false`). `saveExistingMember` honours them; `saveNewMember` ignores them. Task 3 sets them from `MembersPage`.

- [ ] **Step 1: Write the failing tests**

Append to `libraries/Core/test/src/test_member_repository.cpp`, after the `SaveNewMemberRollsBackWhenPhotoFails` test:

```cpp
TEST_F(test_core_MemberRepository, ClearPhotoEmptiesTheColumnAndDeletesTheFile)
{
    const MemberSeed seed = uniqueMemberSeed(40);
    const std::int64_t id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);
    ASSERT_TRUE(m_repository->setPhotoImage(id, writeSampleImage("p.png")));
    ASSERT_TRUE(m_repository->setIdImage(id, writeSampleImage("i.png")));
    const auto before = m_repository->getMember(id);
    ASSERT_TRUE(before.has_value());
    const std::string photoFile = m_repository->resolveImagePath(before->photoPath);
    const std::string idFile = m_repository->resolveImagePath(before->idImagePath);
    ASSERT_TRUE(std::filesystem::exists(photoFile));

    MemberWrite write;
    write.member = seed.toInput();
    write.clearPhoto = true;
    const auto saved = m_repository->saveExistingMember(id, write);
    ASSERT_TRUE(saved) << saved.error().key;

    const auto after = m_repository->getMember(id);
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(after->photoPath.empty());
    EXPECT_FALSE(std::filesystem::exists(photoFile));
    EXPECT_EQ(after->idImagePath, before->idImagePath) << "clearPhoto must not touch the ID image";
    EXPECT_TRUE(std::filesystem::exists(idFile));
}

TEST_F(test_core_MemberRepository, ClearIdImageEmptiesTheColumnAndDeletesTheFile)
{
    const MemberSeed seed = uniqueMemberSeed(41);
    const std::int64_t id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);
    ASSERT_TRUE(m_repository->setPhotoImage(id, writeSampleImage("p.png")));
    ASSERT_TRUE(m_repository->setIdImage(id, writeSampleImage("i.png")));
    const auto before = m_repository->getMember(id);
    ASSERT_TRUE(before.has_value());
    const std::string photoFile = m_repository->resolveImagePath(before->photoPath);
    const std::string idFile = m_repository->resolveImagePath(before->idImagePath);

    MemberWrite write;
    write.member = seed.toInput();
    write.clearIdImage = true;
    const auto saved = m_repository->saveExistingMember(id, write);
    ASSERT_TRUE(saved) << saved.error().key;

    const auto after = m_repository->getMember(id);
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(after->idImagePath.empty());
    EXPECT_FALSE(std::filesystem::exists(idFile));
    EXPECT_EQ(after->photoPath, before->photoPath) << "clearIdImage must not touch the photo";
    EXPECT_TRUE(std::filesystem::exists(photoFile));
}

TEST_F(test_core_MemberRepository, FailedSaveKeepsTheImageItWasToClear)
{
    const MemberSeed seed = uniqueMemberSeed(42);
    const std::int64_t id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);
    ASSERT_TRUE(m_repository->setPhotoImage(id, writeSampleImage("p.png")));
    const auto before = m_repository->getMember(id);
    ASSERT_TRUE(before.has_value());
    const std::string photoFile = m_repository->resolveImagePath(before->photoPath);

    MemberWrite write;
    write.member = seed.toInput();
    write.member.firstName = "  ";  // refused: error.member.namesRequired
    write.clearPhoto = true;
    EXPECT_FALSE(m_repository->saveExistingMember(id, write));

    const auto after = m_repository->getMember(id);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->photoPath, before->photoPath);
    EXPECT_TRUE(std::filesystem::exists(photoFile));
}

TEST_F(test_core_MemberRepository, ClearPhotoWithoutAPhotoSucceeds)
{
    const MemberSeed seed = uniqueMemberSeed(43);
    const std::int64_t id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);

    MemberWrite write;
    write.member = seed.toInput();
    write.clearPhoto = true;
    const auto saved = m_repository->saveExistingMember(id, write);
    ASSERT_TRUE(saved) << saved.error().key;
    EXPECT_TRUE(m_repository->getMember(id)->photoPath.empty());
}

TEST_F(test_core_MemberRepository, ClearPhotoNeverDeletesAFileOutsideResources)
{
    // An absolute stored path came in with imported data. It may be the
    // librarian's own original, so only the column is cleared.
    const MemberSeed seed = uniqueMemberSeed(44);
    const std::int64_t id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);
    const std::string outside = writeSampleImage("outside.png");
    ASSERT_TRUE(std::filesystem::path(outside).is_absolute());
    ASSERT_TRUE(m_db->execBound("UPDATE members SET photo_path = :path WHERE id = :id",
                                {{"path", outside}, {"id", id}}));

    MemberWrite write;
    write.member = seed.toInput();
    write.clearPhoto = true;
    const auto saved = m_repository->saveExistingMember(id, write);
    ASSERT_TRUE(saved) << saved.error().key;

    EXPECT_TRUE(m_repository->getMember(id)->photoPath.empty());
    EXPECT_TRUE(std::filesystem::exists(outside));
}

TEST_F(test_core_MemberRepository, SaveNewMemberIgnoresClearFlags)
{
    MemberWrite write;
    write.member = uniqueMemberSeed(45).toInput();
    write.clearPhoto = true;
    write.clearIdImage = true;
    const auto created = m_repository->saveNewMember(write);
    ASSERT_TRUE(created) << created.error().key;
    EXPECT_TRUE(m_repository->getMember(created.value())->photoPath.empty());
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build -j$(nproc) 2>&1 | grep -E "error" | head`
Expected: compile errors, `'struct MemberWrite' has no member named 'clearPhoto'`.

- [ ] **Step 3: Add the flags to `MemberWrite`**

In `libraries/Core/include/VLMS/Core/MemberTypes.h`, replace:

```cpp
struct MemberWrite {
    MemberInput member;
    std::string photoSourcePath;
    std::string idImageSourcePath;
};
```

with:

```cpp
struct MemberWrite {
    MemberInput member;
    std::string photoSourcePath;
    std::string idImageSourcePath;
    /// Take the stored image off an existing member. Ignored for a new
    /// member, and ignored for a slot that also has a source path.
    bool clearPhoto = false;
    bool clearIdImage = false;
};
```

- [ ] **Step 4: Declare `clearMemberImage`**

In `libraries/Core/include/VLMS/Core/MemberRepository.h`, directly after the `storeMemberImage` declaration in the private section, add:

```cpp
    /// Sets the slot's column to NULL and appends the path it held to
    /// `clearedPaths`, so the caller can delete the file once the
    /// transaction has committed. A slot already empty is left alone.
    [[nodiscard]] VLMS::Status clearMemberImage(std::int64_t memberId,
                                                ImageSlot slot,
                                                std::vector<std::string>& clearedPaths);
```

- [ ] **Step 5: Implement it and use it in `saveExistingMember`**

In `libraries/Core/src/MemberRepository.cpp`, replace the whole of `saveExistingMember` with:

```cpp
Status MemberRepository::saveExistingMember(const std::int64_t id, const MemberWrite& write)
{
    std::vector<std::string> clearedPaths;
    const Status saved = m_session.transaction([&] {
        if (const auto updated = applyMemberFields(id, write.member); !updated) {
            return updated;
        }
        if (!write.photoSourcePath.empty()) {
            if (const auto photo = storeMemberImage(id, write.photoSourcePath, ImageSlot::Photo);
                !photo) {
                return photo;
            }
        } else if (write.clearPhoto) {
            if (const auto cleared = clearMemberImage(id, ImageSlot::Photo, clearedPaths);
                !cleared) {
                return cleared;
            }
        }
        if (!write.idImageSourcePath.empty()) {
            if (const auto idImage = storeMemberImage(id, write.idImageSourcePath, ImageSlot::IdCard);
                !idImage) {
                return idImage;
            }
        } else if (write.clearIdImage) {
            if (const auto cleared = clearMemberImage(id, ImageSlot::IdCard, clearedPaths);
                !cleared) {
                return cleared;
            }
        }
        return Status::ok();
    });

    // Only after the commit: a save that failed must not have lost the file.
    if (saved) {
        for (const std::string& stored : clearedPaths) {
            // An absolute path came in with imported data and may be the
            // librarian's own original, not a copy this repository made.
            if (std::filesystem::path(stored).is_absolute()) {
                continue;
            }
            std::error_code ignored;
            std::filesystem::remove(resolveImagePath(stored), ignored);
        }
    }
    return saved;
}
```

Then, directly after `storeMemberImage` (before `setPhotoImage`), add:

```cpp
Status MemberRepository::clearMemberImage(const std::int64_t memberId,
                                          const ImageSlot slot,
                                          std::vector<std::string>& clearedPaths)
{
    std::string stored;
    {
        auto select = m_session.prepare(slot == ImageSlot::Photo
                                            ? "SELECT photo_path FROM members WHERE id = :id"
                                            : "SELECT id_image_path FROM members WHERE id = :id");
        if (!select) {
            return RepoSql::sqlFailure(select.error().detail);
        }
        if (!select->bind(":id", memberId)) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        if (select->next()) {
            stored = select->text(0);
        }
    }
    if (stored.empty()) {
        return Status::ok();
    }

    const char* sql = slot == ImageSlot::Photo
        ? "UPDATE members SET photo_path = NULL, updated_at = :updated_at WHERE id = :id"
        : "UPDATE members SET id_image_path = NULL, updated_at = :updated_at WHERE id = :id";
    auto update = m_session.prepare(sql);
    if (!update) {
        return RepoSql::sqlFailure(update.error().detail);
    }
    if (!update->bind(":updated_at", Clock::nowIso()) || !update->bind(":id", memberId)
        || !update->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    clearedPaths.push_back(stored);
    return Status::ok();
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build -j$(nproc) 2>&1 | grep -E "error" ; ctest --test-dir build -R 'test_core_MemberRepository' --output-on-failure | tail -3`
Expected: no build errors; `100% tests passed`.

---

### Task 2: `ImageViewerDialog` and its strings

**Files:**
- Create: `applications/vlms/src/ui/ImageViewerDialog.h`
- Create: `applications/vlms/src/ui/ImageViewerDialog.cpp`
- Modify: `applications/vlms/CMakeLists.txt:11` (sources) and `:51` (headers), next to `LicenceDialog`
- Modify: `libraries/Core/src/Strings.cpp` (after `member.imageFilter` in each of the three tables: ~lines 256, 751, 1262)
- Create: `applications/vlms/test/src/test_image_viewer_dialog.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt` (SRC list)

**Interfaces:**
- Consumes: `VLMS::makePrimaryButton`, `VLMS::makeSecondaryButton` from `ui/UiHelpers.h`; `VLMS::T` from `QtBridge.h`.
- Produces (used by Task 3):
  ```cpp
  class ImageViewerDialog final : public QDialog {
  public:
      using FilePicker = std::function<QString()>;          // empty = cancelled
      enum class Outcome { Unchanged, Changed, Removed };
      ImageViewerDialog(const QString& title, const QString& imagePath,
                        FilePicker pickFile, QWidget* parent = nullptr);
      Outcome outcome() const;
      QString imagePath() const;
      QSize shownImageSize() const;
      static QSize fittedSize(const QSize& image, const QSize& bounds);
  };
  ```
  String keys: `member.viewPhoto`, `member.viewIdImage`, `imageViewer.change`, `imageViewer.remove`, `imageViewer.close`.

- [ ] **Step 1: Add the strings**

In `libraries/Core/src/Strings.cpp`, after the Arabic `{"member.imageFilter", ...},` line add:

```cpp
        {"member.viewPhoto", "عرض الصورة"},
        {"member.viewIdImage", "عرض صورة الهوية"},
        {"imageViewer.change", "تغيير…"},
        {"imageViewer.remove", "إزالة"},
        {"imageViewer.close", "إغلاق"},
```

After the French `{"member.imageFilter", ...},` line add:

```cpp
        {"member.viewPhoto", "Voir la photo"},
        {"member.viewIdImage", "Voir la pièce d'identité"},
        {"imageViewer.change", "Changer…"},
        {"imageViewer.remove", "Retirer"},
        {"imageViewer.close", "Fermer"},
```

After the English `{"member.imageFilter", ...},` line add:

```cpp
        {"member.viewPhoto", "View photo"},
        {"member.viewIdImage", "View ID image"},
        {"imageViewer.change", "Change…"},
        {"imageViewer.remove", "Remove"},
        {"imageViewer.close", "Close"},
```

- [ ] **Step 2: Write the failing viewer tests**

Create `applications/vlms/test/src/test_image_viewer_dialog.cpp`:

```cpp
#include "ModalTest.h"

#include "ui/ImageViewerDialog.h"

#include <VLMS/Core/Locale.h>

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>

using VLMS::Locale;

class test_ui_ImageViewerDialog : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }

    void SetUp() override { ASSERT_TRUE(m_dir.isValid()); }

    QString writeImage(const QString& name, const QSize& size, const QColor& colour = Qt::darkCyan)
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(colour);
        const QString path = m_dir.filePath(name);
        EXPECT_TRUE(image.save(path));
        return path;
    }

    static QSize available()
    {
        return QGuiApplication::primaryScreen()->availableGeometry().size();
    }

    QTemporaryDir m_dir;
};

TEST_F(test_ui_ImageViewerDialog, FittedSizeScalesUpAndDown)
{
    EXPECT_EQ(ImageViewerDialog::fittedSize({200, 260}, {620, 420}), QSize(323, 420));
    EXPECT_EQ(ImageViewerDialog::fittedSize({4000, 3000}, {620, 420}), QSize(560, 420));
    EXPECT_EQ(ImageViewerDialog::fittedSize({100, 10}, {620, 420}), QSize(620, 62));
    EXPECT_TRUE(ImageViewerDialog::fittedSize({}, {620, 420}).isEmpty());
}

TEST_F(test_ui_ImageViewerDialog, SmallImageOpensLargerThanItsFile)
{
    ImageViewerDialog viewer(QStringLiteral("Photo"), writeImage("small.png", {200, 260}), {});
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));

    const QSize shown = viewer.shownImageSize();
    EXPECT_GT(shown.width(), 200);
    EXPECT_GT(shown.height(), 260);
    // It fills the 80 % box on its limiting side, give or take the buttons and margins.
    EXPECT_GE(shown.height(), available().height() * 0.8 - 120);
}

TEST_F(test_ui_ImageViewerDialog, LargeImageShrinksToFitTheScreen)
{
    ImageViewerDialog viewer(QStringLiteral("Photo"), writeImage("large.png", {4000, 3000}), {});
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));

    const QSize shown = viewer.shownImageSize();
    EXPECT_LE(shown.width(), available().width());
    EXPECT_LE(shown.height(), available().height());
    EXPECT_NEAR(double(shown.width()) / shown.height(), 4.0 / 3.0, 0.02);
}

TEST_F(test_ui_ImageViewerDialog, ResizingTheWindowRescalesTheImage)
{
    ImageViewerDialog viewer(QStringLiteral("Photo"), writeImage("photo.png", {300, 400}), {});
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));
    const QSize before = viewer.shownImageSize();

    viewer.resize(viewer.width() / 2, viewer.height() / 2);
    QTest::qWait(50);

    EXPECT_LT(viewer.shownImageSize().height(), before.height());
}

TEST_F(test_ui_ImageViewerDialog, ChangeShowsThePickedImageAndStaysOpen)
{
    const QString first = writeImage("first.png", {300, 400});
    const QString second = writeImage("second.png", {400, 300}, Qt::darkRed);
    ImageViewerDialog viewer(QStringLiteral("Photo"), first, [second] { return second; });
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));

    clickButtonWithText(&viewer, QStringLiteral("Change…"));

    EXPECT_TRUE(viewer.isVisible());
    EXPECT_EQ(viewer.outcome(), ImageViewerDialog::Outcome::Changed);
    EXPECT_EQ(viewer.imagePath(), second);
    const QSize shown = viewer.shownImageSize();
    EXPECT_GT(shown.width(), shown.height()) << "the landscape image is the one on screen";
}

TEST_F(test_ui_ImageViewerDialog, CancelledChangeLeavesItUnchanged)
{
    const QString first = writeImage("first.png", {300, 400});
    ImageViewerDialog viewer(QStringLiteral("Photo"), first, [] { return QString(); });
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));

    clickButtonWithText(&viewer, QStringLiteral("Change…"));

    EXPECT_EQ(viewer.outcome(), ImageViewerDialog::Outcome::Unchanged);
    EXPECT_EQ(viewer.imagePath(), first);
}

TEST_F(test_ui_ImageViewerDialog, ChangeToAFileThatIsNoImageIsIgnored)
{
    const QString first = writeImage("first.png", {300, 400});
    const QString notAnImage = m_dir.filePath("notes.png");
    {
        QFile file(notAnImage);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("not a picture");
    }
    ImageViewerDialog viewer(QStringLiteral("Photo"), first, [notAnImage] { return notAnImage; });
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));

    clickButtonWithText(&viewer, QStringLiteral("Change…"));

    EXPECT_EQ(viewer.outcome(), ImageViewerDialog::Outcome::Unchanged);
    EXPECT_EQ(viewer.imagePath(), first);
}

TEST_F(test_ui_ImageViewerDialog, RemoveClosesTheViewer)
{
    ImageViewerDialog viewer(QStringLiteral("Photo"), writeImage("photo.png", {300, 400}), {});
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));

    clickButtonWithText(&viewer, QStringLiteral("Remove"));

    EXPECT_FALSE(viewer.isVisible());
    EXPECT_EQ(viewer.outcome(), ImageViewerDialog::Outcome::Removed);
}

TEST_F(test_ui_ImageViewerDialog, CloseClosesWithoutChange)
{
    ImageViewerDialog viewer(QStringLiteral("Photo"), writeImage("photo.png", {300, 400}), {});
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));

    clickButtonWithText(&viewer, QStringLiteral("Close"));

    EXPECT_FALSE(viewer.isVisible());
    EXPECT_EQ(viewer.outcome(), ImageViewerDialog::Outcome::Unchanged);
}
```

In `applications/vlms/test/CMakeLists.txt`, add `src/test_image_viewer_dialog.cpp` to the `SRC` list after `src/test_licence_dialog.cpp`.

- [ ] **Step 3: Run the tests to verify they fail**

Run: `cmake --build build -j$(nproc) 2>&1 | grep -E "error" | head -3`
Expected: `fatal error: ui/ImageViewerDialog.h: No such file or directory`.

- [ ] **Step 4: Write the header**

Create `applications/vlms/src/ui/ImageViewerDialog.h`:

```cpp
#pragma once

#include <QDialog>
#include <QPixmap>
#include <QSize>
#include <QString>

#include <functional>

class QLabel;

/**
 * One image, shown large, with Change…, Remove and Close.
 *
 * Opened by MemberEditorDialog when a librarian clicks a photo or ID image
 * box that already shows an image. It knows nothing about members: it is
 * given a path and a way to pick another file, and reports what happened.
 * It writes nothing; the caller decides what the outcome means.
 *
 * No retranslateUi: it is modal over a modal dialog, so the language
 * selector cannot be reached while it is open. LicenceDialog is the same.
 */
class ImageViewerDialog final : public QDialog {
    Q_OBJECT

public:
    /// Returns the chosen file, or an empty string when the picker was cancelled.
    using FilePicker = std::function<QString()>;

    enum class Outcome { Unchanged, Changed, Removed };

    ImageViewerDialog(const QString& title,
                      const QString& imagePath,
                      FilePicker pickFile,
                      QWidget* parent = nullptr);

    [[nodiscard]] Outcome outcome() const { return m_outcome; }
    /// The image on screen: the one it opened with, or the last one Change picked.
    [[nodiscard]] QString imagePath() const { return m_path; }
    /// The size of the scaled pixmap on screen. For tests.
    [[nodiscard]] QSize shownImageSize() const;

    /// The largest size `image` scales to inside `bounds`, up or down,
    /// keeping its aspect ratio. Empty when either size is empty.
    [[nodiscard]] static QSize fittedSize(const QSize& image, const QSize& bounds);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi(const QString& title);
    void sizeToScreen();
    void rescale();
    void changeImage();

    FilePicker m_pickFile;
    QString m_path;
    QPixmap m_source;
    Outcome m_outcome = Outcome::Unchanged;
    QLabel* m_image = nullptr;
    QWidget* m_buttonRow = nullptr;
};
```

- [ ] **Step 5: Write the implementation**

Create `applications/vlms/src/ui/ImageViewerDialog.cpp`:

```cpp
#include "ui/ImageViewerDialog.h"

#include "ui/UiHelpers.h"
#include "QtBridge.h"

#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include <utility>

using VLMS::T;

namespace {

/// Share of the screen's available area the viewer opens at.
constexpr double kScreenShare = 0.8;
/// The window stops shrinking here; the image still fits inside it.
constexpr int kMinImageSide = 120;

}  // namespace

ImageViewerDialog::ImageViewerDialog(const QString& title,
                                     const QString& imagePath,
                                     FilePicker pickFile,
                                     QWidget* parent)
    : QDialog(parent),
      m_pickFile(std::move(pickFile)),
      m_path(imagePath),
      m_source(imagePath)
{
    buildUi(title);
    sizeToScreen();
}

QSize ImageViewerDialog::fittedSize(const QSize& image, const QSize& bounds)
{
    if (image.isEmpty() || bounds.isEmpty()) {
        return {};
    }
    return image.scaled(bounds, Qt::KeepAspectRatio);
}

QSize ImageViewerDialog::shownImageSize() const
{
    return m_image->pixmap().size();
}

void ImageViewerDialog::buildUi(const QString& title)
{
    setObjectName(QStringLiteral("imageViewer"));
    setWindowTitle(title);

    auto* layout = new QVBoxLayout(this);

    m_image = new QLabel(this);
    m_image->setObjectName(QStringLiteral("imageViewerImage"));
    m_image->setAlignment(Qt::AlignCenter);
    // The pixmap is redrawn to fit the label, so it must not also size the
    // label: a pixmap-sized minimum would stop the window from shrinking.
    m_image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_image->setMinimumSize(kMinImageSide, kMinImageSide);
    m_image->installEventFilter(this);
    layout->addWidget(m_image, 1);

    auto* change = VLMS::makeSecondaryButton(T("imageViewer.change"));
    auto* remove = VLMS::makeSecondaryButton(T("imageViewer.remove"));
    auto* close = VLMS::makePrimaryButton(T("imageViewer.close"));
    close->setDefault(true);

    connect(change, &QPushButton::clicked, this, &ImageViewerDialog::changeImage);
    connect(remove, &QPushButton::clicked, this, [this]() {
        m_outcome = Outcome::Removed;
        accept();
    });
    connect(close, &QPushButton::clicked, this, [this]() {
        if (m_outcome == Outcome::Unchanged) {
            reject();
        } else {
            accept();
        }
    });

    m_buttonRow = new QWidget(this);
    auto* buttons = new QHBoxLayout(m_buttonRow);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addWidget(change);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    buttons->addWidget(close);
    layout->addWidget(m_buttonRow);
}

void ImageViewerDialog::sizeToScreen()
{
    QScreen* screen = parentWidget() != nullptr ? parentWidget()->screen()
                                                : QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return;
    }

    const QMargins margins = layout()->contentsMargins();
    const QSize chrome(margins.left() + margins.right(),
                       margins.top() + margins.bottom() + layout()->spacing()
                           + m_buttonRow->sizeHint().height());
    const QSize bounds = screen->availableGeometry().size() * kScreenShare - chrome;

    QSize image = fittedSize(m_source.size(), bounds);
    if (image.isEmpty()) {
        image = QSize(kMinImageSide, kMinImageSide);
    }
    resize((image + chrome).expandedTo(minimumSizeHint()));
}

bool ImageViewerDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_image && event->type() == QEvent::Resize) {
        rescale();
    }
    return QDialog::eventFilter(watched, event);
}

void ImageViewerDialog::rescale()
{
    if (m_source.isNull()) {
        m_image->clear();
        return;
    }
    // Always from the original, so repeated resizing never compounds losses.
    m_image->setPixmap(
        m_source.scaled(m_image->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ImageViewerDialog::changeImage()
{
    const QString path = m_pickFile ? m_pickFile() : QString();
    if (path.isEmpty()) {
        return;
    }
    const QPixmap picked(path);
    if (picked.isNull()) {
        return;
    }
    m_path = path;
    m_source = picked;
    m_outcome = Outcome::Changed;
    rescale();
}
```

In `applications/vlms/CMakeLists.txt`, add `src/ui/ImageViewerDialog.cpp` on the line after `src/ui/LicenceDialog.cpp`, and `src/ui/ImageViewerDialog.h` on the line after `src/ui/LicenceDialog.h`.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build -j$(nproc) 2>&1 | grep -E "error" ; ctest --test-dir build -R 'test_ui_ImageViewerDialog|StringsParity|strings_parity' --output-on-failure | tail -3`
Expected: no build errors; `100% tests passed`.

---

### Task 3: Picker or viewer in `MemberEditorDialog`, and saving a removal

**Files:**
- Modify: `applications/vlms/src/ui/members/MemberEditorDialog.h`
- Modify: `applications/vlms/src/ui/members/MemberEditorDialog.cpp` (helpers ~61-77, `retranslateUi` ~334-349, `choosePhotoImage`/`chooseIdImage` ~402-430, `refresh…PreviewPixmap` ~444-475, `eventFilter` ~522-535)
- Modify: `applications/vlms/src/ui/members/MembersPage.cpp:482-487` and `:520-525`
- Create: `applications/vlms/test/src/test_member_image_viewer.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt` (SRC list)

**Interfaces:**
- Consumes: `ImageViewerDialog` (Task 2) exactly as declared there; `MemberWrite::clearPhoto` / `clearIdImage` (Task 1); string keys from Task 2.
- Produces:
  ```cpp
  using ImageFilePicker = std::function<QString(QWidget* parent, const QString& title, const QString& nameFilter)>;
  static void MemberEditorDialog::setImageFilePickerForTesting(ImageFilePicker picker);  // empty = VLMS::askForImageFile
  bool MemberEditorDialog::photoRemoved() const;
  bool MemberEditorDialog::idImageRemoved() const;
  ```

- [ ] **Step 1: Write the failing tests**

Create `applications/vlms/test/src/test_member_image_viewer.cpp`:

```cpp
#include "ModalTest.h"
#include "TestDatabase.h"

#include "ui/ImageViewerDialog.h"
#include "ui/members/MemberEditorDialog.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/MemberRepository.h>
#include <VLMS/Core/MemberTypes.h>

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <gtest/gtest.h>

#include <functional>
#include <memory>

using VLMS::Locale;
using namespace VLMS::Test;

namespace {

/// Schedule this only after the member dialog is shown: the zero-delay timer
/// fires at the next event loop, and qWaitForWindowExposed runs one. Then the
/// next loop is the viewer's own exec(), so `handle` finds it.
/// Runs `handle` on the ImageViewerDialog once it is the active modal.
/// Hands it nullptr when no viewer appeared.
void onViewer(std::function<void(ImageViewerDialog*)> handle)
{
    QTimer::singleShot(0, [handle = std::move(handle)]() {
        ImageViewerDialog* viewer = nullptr;
        for (int attempt = 0; attempt < 100 && viewer == nullptr; ++attempt) {
            viewer = qobject_cast<ImageViewerDialog*>(QApplication::activeModalWidget());
            if (viewer == nullptr) {
                QTest::qWait(10);
            }
        }
        handle(viewer);
    });
}

/// For tests that expect no viewer: if one opens anyway, this closes it so
/// the test fails on its assertions instead of hanging. It captures nothing,
/// so firing after the test has ended is harmless.
void closeAnyViewerSoon()
{
    QTimer::singleShot(2000, []() {
        if (auto* viewer = qobject_cast<ImageViewerDialog*>(QApplication::activeModalWidget())) {
            viewer->reject();
        }
    });
}

}  // namespace

class test_ui_MemberImageViewer : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }

    void SetUp() override
    {
        ASSERT_TRUE(m_dir.isValid());
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_repository = std::make_unique<MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_pickerCalls = 0;
        m_pickerAnswer.clear();
        MemberEditorDialog::setImageFilePickerForTesting(
            [this](QWidget*, const QString&, const QString&) {
                ++m_pickerCalls;
                return m_pickerAnswer;
            });
    }

    void TearDown() override
    {
        MemberEditorDialog::setImageFilePickerForTesting({});
        m_repository.reset();
        m_db.reset();
    }

    QString writeImage(const QString& name, const QSize& size = {200, 260})
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        const QString path = m_dir.filePath(name);
        EXPECT_TRUE(image.save(path));
        return path;
    }

    static MemberRecord memberWithPhoto(const QString& photoPath, const QString& idImagePath = {})
    {
        MemberRecord member;
        member.id = 1;
        member.membershipNumber = "1";
        member.firstName = "Amina";
        member.lastName = "Ben Salah";
        member.dateOfBirth = "1990-05-12";
        member.status = MemberStatus::kActive;
        member.activeUntil = "2099-01-01";
        // Absolute paths: resolveImagePath passes them through unchanged.
        member.photoPath = photoPath.toStdString();
        member.idImagePath = idImagePath.toStdString();
        return member;
    }

    static QLabel* box(MemberEditorDialog& dialog, const char* name)
    {
        return dialog.findChild<QLabel*>(QString::fromLatin1(name));
    }

    static void showDialog(MemberEditorDialog& dialog)
    {
        dialog.show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(&dialog));
    }

    static void click(QLabel* target) { QTest::mouseClick(target, Qt::LeftButton); }

    QTemporaryDir m_dir;
    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<MemberRepository> m_repository;
    int m_pickerCalls = 0;
    QString m_pickerAnswer;
};

TEST_F(test_ui_MemberImageViewer, AnEmptyBoxOpensThePicker)
{
    MemberEditorDialog dialog(*m_repository);
    showDialog(dialog);
    closeAnyViewerSoon();

    click(box(dialog, "memberPhoto"));

    EXPECT_EQ(m_pickerCalls, 1) << "an empty box goes to the picker, not the viewer";
    EXPECT_EQ(box(dialog, "memberPhoto")->toolTip(), QStringLiteral("Choose photo…"));
}

TEST_F(test_ui_MemberImageViewer, ABoxWhoseFileIsNoImageOpensThePicker)
{
    const QString notAnImage = m_dir.filePath("notes.png");
    {
        QFile file(notAnImage);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("not a picture");
    }
    MemberEditorDialog dialog(*m_repository, memberWithPhoto(notAnImage));
    showDialog(dialog);
    closeAnyViewerSoon();

    click(box(dialog, "memberPhoto"));

    EXPECT_EQ(m_pickerCalls, 1) << "an unreadable image counts as no image";
}

TEST_F(test_ui_MemberImageViewer, ABoxShowingAnImageOpensTheViewer)
{
    MemberEditorDialog dialog(*m_repository, memberWithPhoto(writeImage("photo.png")));
    EXPECT_EQ(box(dialog, "memberPhoto")->toolTip(), QStringLiteral("View photo"));
    showDialog(dialog);

    QString title;
    onViewer([&title](ImageViewerDialog* viewer) {
        ASSERT_NE(viewer, nullptr);
        title = viewer->windowTitle();
        clickButtonWithText(viewer, QStringLiteral("Close"));
    });

    click(box(dialog, "memberPhoto"));

    EXPECT_EQ(m_pickerCalls, 0);
    EXPECT_EQ(title, QStringLiteral("Photo"));
    EXPECT_FALSE(dialog.photoChanged());
    EXPECT_FALSE(dialog.photoRemoved());
}

TEST_F(test_ui_MemberImageViewer, TheIdImageBoxOpensItsOwnViewer)
{
    MemberEditorDialog dialog(*m_repository,
                              memberWithPhoto({}, writeImage("id.png", {400, 250})));
    EXPECT_EQ(box(dialog, "memberIdImage")->toolTip(), QStringLiteral("View ID image"));
    showDialog(dialog);

    QString title;
    onViewer([&title](ImageViewerDialog* viewer) {
        ASSERT_NE(viewer, nullptr);
        title = viewer->windowTitle();
        clickButtonWithText(viewer, QStringLiteral("Close"));
    });

    click(box(dialog, "memberIdImage"));

    EXPECT_EQ(m_pickerCalls, 0);
    EXPECT_EQ(title, QStringLiteral("ID image"));
}

TEST_F(test_ui_MemberImageViewer, ChangeInTheViewerIsKeptAfterClose)
{
    MemberEditorDialog dialog(*m_repository, memberWithPhoto(writeImage("old.png")));
    showDialog(dialog);
    m_pickerAnswer = writeImage("new.png", {300, 300});

    bool stayedOpen = false;
    onViewer([&stayedOpen](ImageViewerDialog* viewer) {
        ASSERT_NE(viewer, nullptr);
        clickButtonWithText(viewer, QStringLiteral("Change…"));
        stayedOpen = viewer->isVisible();
        clickButtonWithText(viewer, QStringLiteral("Close"));
    });

    click(box(dialog, "memberPhoto"));

    EXPECT_EQ(m_pickerCalls, 1);
    EXPECT_TRUE(stayedOpen);
    EXPECT_TRUE(dialog.photoChanged());
    EXPECT_FALSE(dialog.photoRemoved());
    EXPECT_EQ(dialog.photoSourcePath(), m_pickerAnswer);
}

TEST_F(test_ui_MemberImageViewer, CancelledChangeChangesNothing)
{
    MemberEditorDialog dialog(*m_repository, memberWithPhoto(writeImage("old.png")));
    showDialog(dialog);
    m_pickerAnswer.clear();

    onViewer([](ImageViewerDialog* viewer) {
        ASSERT_NE(viewer, nullptr);
        clickButtonWithText(viewer, QStringLiteral("Change…"));
        clickButtonWithText(viewer, QStringLiteral("Close"));
    });

    click(box(dialog, "memberPhoto"));

    EXPECT_EQ(m_pickerCalls, 1);
    EXPECT_FALSE(dialog.photoChanged());
    EXPECT_FALSE(dialog.photoRemoved());
}

TEST_F(test_ui_MemberImageViewer, RemoveEmptiesTheBox)
{
    MemberEditorDialog dialog(*m_repository, memberWithPhoto(writeImage("old.png")));
    showDialog(dialog);

    onViewer([](ImageViewerDialog* viewer) {
        ASSERT_NE(viewer, nullptr);
        clickButtonWithText(viewer, QStringLiteral("Remove"));
    });

    click(box(dialog, "memberPhoto"));

    EXPECT_TRUE(dialog.photoRemoved());
    EXPECT_FALSE(dialog.photoChanged());
    EXPECT_TRUE(dialog.photoSourcePath().isEmpty());
    EXPECT_EQ(box(dialog, "memberPhoto")->toolTip(), QStringLiteral("Choose photo…"));
    bool buttonReset = false;
    for (QPushButton* button : dialog.findChildren<QPushButton*>()) {
        buttonReset = buttonReset || button->text() == QStringLiteral("Choose photo…");
    }
    EXPECT_TRUE(buttonReset) << "the picker button must no longer name the removed file";
}

TEST_F(test_ui_MemberImageViewer, APickAfterRemoveWins)
{
    MemberEditorDialog dialog(*m_repository, memberWithPhoto(writeImage("old.png")));
    showDialog(dialog);
    onViewer([](ImageViewerDialog* viewer) {
        ASSERT_NE(viewer, nullptr);
        clickButtonWithText(viewer, QStringLiteral("Remove"));
    });
    click(box(dialog, "memberPhoto"));
    ASSERT_TRUE(dialog.photoRemoved());

    // The box is empty now, so the next click goes straight to the picker.
    m_pickerAnswer = writeImage("new.png");
    closeAnyViewerSoon();
    click(box(dialog, "memberPhoto"));

    EXPECT_EQ(m_pickerCalls, 1);
    EXPECT_TRUE(dialog.photoChanged());
    EXPECT_FALSE(dialog.photoRemoved());
    EXPECT_EQ(dialog.photoSourcePath(), m_pickerAnswer);
}
```

In `applications/vlms/test/CMakeLists.txt`, add `src/test_member_image_viewer.cpp` to the `SRC` list after `src/test_member_editor_status.cpp`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build -j$(nproc) 2>&1 | grep -E "error" | head -3`
Expected: `'setImageFilePickerForTesting' is not a member of 'MemberEditorDialog'`.

- [ ] **Step 3: Extend the header**

In `applications/vlms/src/ui/members/MemberEditorDialog.h`:

Add `#include <functional>` after `#include <QString>`.

In the public section, after `[[nodiscard]] bool idImageChanged() const { return m_idImageChanged; }`, add:

```cpp
    [[nodiscard]] bool photoRemoved() const { return m_photoRemoved; }
    [[nodiscard]] bool idImageRemoved() const { return m_idImageRemoved; }

    /// The file picker every image choice goes through. Tests swap it for a
    /// function that answers at once, since a real QFileDialog would block.
    using ImageFilePicker =
        std::function<QString(QWidget* parent, const QString& title, const QString& nameFilter)>;
    /// An empty picker restores VLMS::askForImageFile.
    static void setImageFilePickerForTesting(ImageFilePicker picker);
```

In the private section, replace:

```cpp
    void choosePhotoImage();
    void chooseIdImage();
```

with:

```cpp
    void choosePhotoImage();
    void chooseIdImage();
    /// The picker, or its test stand-in. Empty when cancelled.
    QString pickImageFile(const QString& title);
    void applyPhotoImage(const QString& path);
    void applyIdImage(const QString& path);
    void removePhotoImage();
    void removeIdImage();
    void viewPhotoImage();
    void viewIdImage();
```

and after `bool m_idImageChanged = false;` add:

```cpp
    bool m_photoRemoved = false;
    bool m_idImageRemoved = false;
    /// Whether the box shows a real image rather than its placeholder --
    /// what decides between the viewer and the picker on a click.
    bool m_photoShown = false;
    bool m_idImageShown = false;
```

- [ ] **Step 4: Implement in the .cpp**

In `applications/vlms/src/ui/members/MemberEditorDialog.cpp`:

(a) Add `#include "ui/ImageViewerDialog.h"` after `#include "ui/members/BirthDateEdit.h"`, and `#include <utility>` after the Qt includes.

(b) In the anonymous namespace, make `showImagePreview` report whether it showed an image, and add the picker slot. Replace the whole `showImagePreview` function with:

```cpp
/// True when the image loaded; false when the placeholder is shown instead.
bool showImagePreview(QLabel* label, const QString& imagePath, const QSize& size)
{
    if (imagePath.isEmpty()) {
        showImagePlaceholder(label, {}, size);
        return false;
    }

    const QPixmap pixmap(imagePath);
    if (pixmap.isNull()) {
        showImagePlaceholder(label, {}, size);
        return false;
    }

    label->setText({});
    label->setPixmap(pixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    return true;
}

MemberEditorDialog::ImageFilePicker& imageFilePicker()
{
    static MemberEditorDialog::ImageFilePicker picker;
    return picker;
}
```

(c) After the two constructors, add:

```cpp
void MemberEditorDialog::setImageFilePickerForTesting(ImageFilePicker picker)
{
    imageFilePicker() = std::move(picker);
}

QString MemberEditorDialog::pickImageFile(const QString& title)
{
    if (imageFilePicker()) {
        return imageFilePicker()(this, title, T("member.imageFilter"));
    }
    return VLMS::askForImageFile(this, title, T("member.imageFilter"));
}
```

(d) In `retranslateUi`, delete these lines (the refresh calls at the end of `retranslateUi` now set the tooltips):

```cpp
    if (m_photoPreview != nullptr) {
        m_photoPreview->setToolTip(T("member.choosePhoto"));
    }
    if (m_idImagePreview != nullptr) {
        m_idImagePreview->setToolTip(T("member.chooseIdImage"));
    }
```

(e) Replace `choosePhotoImage` and `chooseIdImage` with:

```cpp
void MemberEditorDialog::choosePhotoImage()
{
    const QString path = pickImageFile(T("member.selectPhoto"));
    if (!path.isEmpty()) {
        applyPhotoImage(path);
    }
}

void MemberEditorDialog::chooseIdImage()
{
    const QString path = pickImageFile(T("member.selectIdImage"));
    if (!path.isEmpty()) {
        applyIdImage(path);
    }
}

void MemberEditorDialog::applyPhotoImage(const QString& path)
{
    m_photoSourcePath = path;
    m_photoChanged = true;
    m_photoRemoved = false;
    m_photoButton->setText(QFileInfo(path).fileName());
    updatePhotoPreview(path);
}

void MemberEditorDialog::applyIdImage(const QString& path)
{
    m_idImageSourcePath = path;
    m_idImageChanged = true;
    m_idImageRemoved = false;
    m_idImageButton->setText(QFileInfo(path).fileName());
    updateIdImagePreview(path);
}

void MemberEditorDialog::removePhotoImage()
{
    m_photoSourcePath.clear();
    m_photoChanged = false;
    m_photoRemoved = true;
    m_photoButton->setText(T("member.choosePhoto"));
    updatePhotoPreview({});
}

void MemberEditorDialog::removeIdImage()
{
    m_idImageSourcePath.clear();
    m_idImageChanged = false;
    m_idImageRemoved = true;
    m_idImageButton->setText(T("member.chooseIdImage"));
    updateIdImagePreview({});
}

void MemberEditorDialog::viewPhotoImage()
{
    ImageViewerDialog viewer(
        T("member.field.photo"),
        m_photoDisplayPath,
        [this]() { return pickImageFile(T("member.selectPhoto")); },
        this);
    viewer.exec();
    switch (viewer.outcome()) {
    case ImageViewerDialog::Outcome::Changed:
        applyPhotoImage(viewer.imagePath());
        break;
    case ImageViewerDialog::Outcome::Removed:
        removePhotoImage();
        break;
    case ImageViewerDialog::Outcome::Unchanged:
        break;
    }
}

void MemberEditorDialog::viewIdImage()
{
    ImageViewerDialog viewer(
        T("member.field.idImage"),
        m_idImageDisplayPath,
        [this]() { return pickImageFile(T("member.selectIdImage")); },
        this);
    viewer.exec();
    switch (viewer.outcome()) {
    case ImageViewerDialog::Outcome::Changed:
        applyIdImage(viewer.imagePath());
        break;
    case ImageViewerDialog::Outcome::Removed:
        removeIdImage();
        break;
    case ImageViewerDialog::Outcome::Unchanged:
        break;
    }
}
```

(f) Replace `refreshPhotoPreviewPixmap` and `refreshIdImagePreviewPixmap` with:

```cpp
void MemberEditorDialog::refreshPhotoPreviewPixmap()
{
    if (m_photoPreview == nullptr) {
        return;
    }

    const QSize previewSize = m_photoPreview->size();
    if (m_photoDisplayPath.isEmpty()) {
        showImagePlaceholder(m_photoPreview, T("members.noPhoto"), previewSize);
        m_photoShown = false;
    } else {
        m_photoShown = showImagePreview(m_photoPreview, m_photoDisplayPath, previewSize);
    }
    m_photoPreview->setToolTip(m_photoShown ? T("member.viewPhoto") : T("member.choosePhoto"));
}

void MemberEditorDialog::refreshIdImagePreviewPixmap()
{
    if (m_idImagePreview == nullptr) {
        return;
    }

    const QSize previewSize = m_idImagePreview->size();
    if (m_idImageDisplayPath.isEmpty()) {
        showImagePlaceholder(
            m_idImagePreview,
            T("member.field.idImage"),
            previewSize);
        m_idImageShown = false;
    } else {
        m_idImageShown = showImagePreview(m_idImagePreview, m_idImageDisplayPath, previewSize);
    }
    m_idImagePreview->setToolTip(m_idImageShown ? T("member.viewIdImage")
                                                : T("member.chooseIdImage"));
}
```

(g) Replace the body of `eventFilter` with:

```cpp
bool MemberEditorDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        if (watched == m_photoPreview) {
            if (m_photoShown) {
                viewPhotoImage();
            } else {
                choosePhotoImage();
            }
            return true;
        }
        if (watched == m_idImagePreview) {
            if (m_idImageShown) {
                viewIdImage();
            } else {
                chooseIdImage();
            }
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
```

- [ ] **Step 5: Pass the removal into the save**

In `applications/vlms/src/ui/members/MembersPage.cpp`, in **both** `addMember()` and `editMember()`, after the block:

```cpp
    if (dialog.idImageChanged()) {
        write.idImageSourcePath = ss(dialog.idImageSourcePath());
    }
```

add:

```cpp
    write.clearPhoto = dialog.photoRemoved();
    write.clearIdImage = dialog.idImageRemoved();
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build -j$(nproc) 2>&1 | grep -E "error" ; ctest --test-dir build -R 'test_ui_MemberImageViewer|test_ui_MemberEditor|test_ui_ImageViewerDialog' --output-on-failure | tail -3`
Expected: no build errors; `100% tests passed`.

---

### Task 4: Full verification

**Files:** none changed.

- [ ] **Step 1: Run the whole suite**

Run: `cmake --build build -j$(nproc) 2>&1 | grep -E "error|warning: " ; ctest --test-dir build -j$(nproc) 2>&1 | grep -E "tests passed|Failed|\*\*\*"`
Expected: no errors or new warnings; `100% tests passed, 0 tests failed`.

- [ ] **Step 2: Check it by hand in the running app**

Run: `build/bin/vlms`, open Members → Edit on a member with a photo.
- Click the photo box: the viewer opens large (most of the screen height), titled "Photo".
- Change… → pick another image: it shows in the viewer, which stays open. Close: the box shows the new image.
- Click the box again → Remove: the box shows "No photo". Cancel the member dialog, reopen: the photo is still there.
- Remove again and press OK: reopen the member, the box is empty; the file under `resources/members/<id>/` (project root) is gone.
- Click an empty ID image box: the file picker opens directly.
- Repeat one click in Arabic and French to check the button labels.
