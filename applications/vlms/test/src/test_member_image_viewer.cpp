#include "ModalTest.h"
#include "TestDatabase.h"

#include "ui/ImageViewerDialog.h"
#include "ui/members/MemberEditorDialog.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MemberTypes.h>

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

/// Schedule this after showDialog: its qWaitForWindowExposed spins the event
/// loop once, so this zero-delay timer fires next, before QTest::mouseClick
/// runs (mouseClick does not spin the loop itself while
/// QTEST_MOUSEEVENT_DELAY is unset). The click that follows opens the viewer
/// modally, which runs its own exec() as the next event loop, and that is
/// where this timer's polling finds it via activeModalWidget(). Runs `handle`
/// on the ImageViewerDialog once it is the active modal. If none appears
/// within the poll window, reports a failure and arms closeAnyViewerSoon()
/// so a viewer that opens later does not block the test forever.
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
        if (viewer == nullptr) {
            ADD_FAILURE() << "no ImageViewerDialog appeared";
            closeAnyViewerSoon();
            return;
        }
        handle(viewer);
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
