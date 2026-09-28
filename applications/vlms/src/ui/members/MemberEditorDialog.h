#pragma once

#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MemberTypes.h>

#include <QDialog>
#include <QList>
#include <QString>

#include <functional>

class BirthDateEdit;
class QComboBox;
class QDialogButtonBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QWidget;

class MemberEditorDialog final : public QDialog {
    Q_OBJECT

public:
    MemberEditorDialog(MemberRepository& repository, QWidget* parent = nullptr);
    MemberEditorDialog(MemberRepository& repository, const MemberRecord& member, QWidget* parent = nullptr);

    /// The first rule the current field values break: the string-table key of
    /// the message to show, and the control to put the cursor back into. Both
    /// empty when the input is acceptable.
    ///
    /// Public because the accept path pops a MODAL message box on failure and
    /// a test that clicked OK would hang on it; this is the same decision the
    /// lambda makes, reachable without a window.
    struct ValidationFailure {
        QString messageKey;
        QWidget* field = nullptr;
    };
    [[nodiscard]] ValidationFailure firstValidationFailure() const;

    [[nodiscard]] MemberInput memberInput() const;
    [[nodiscard]] QString photoSourcePath() const { return m_photoSourcePath; }
    [[nodiscard]] QString idImageSourcePath() const { return m_idImageSourcePath; }
    [[nodiscard]] bool photoChanged() const { return m_photoChanged; }
    [[nodiscard]] bool idImageChanged() const { return m_idImageChanged; }
    [[nodiscard]] bool photoRemoved() const { return m_photoRemoved; }
    [[nodiscard]] bool idImageRemoved() const { return m_idImageRemoved; }

    /// The file picker every image choice goes through. Tests swap it for a
    /// function that answers at once, since a real QFileDialog would block.
    using ImageFilePicker =
        std::function<QString(QWidget* parent, const QString& title, const QString& nameFilter)>;
    /// An empty picker restores VLMS::askForImageFile.
    static void setImageFilePickerForTesting(ImageFilePicker picker);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// A field label and the string key it is re-read from on a language change.
    struct FieldLabel {
        QLabel* label = nullptr;
        QString key;
    };

    void buildUi();
    /// Creates a label for `key` and registers it for retranslation.
    QLabel* addFieldLabel(const QString& key);
    /// One label and one control, the control spanning to the right edge.
    void addWideRow(int row, const QString& labelKey, QWidget* field);
    /// Two label/control pairs on one line, sharing the width evenly.
    void addPairedRow(int row,
                      const QString& labelKey,
                      QWidget* field,
                      const QString& secondLabelKey,
                      QWidget* secondField);
    void retranslateUi();
    void growToLayoutMinimum();
    void populateStatuses();
    void populateSexes();
    void loadMember(const MemberRecord& member);
    void updateActiveUntil();
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
    void updatePhotoPreview(const QString& imagePath = {});
    void updateIdImagePreview(const QString& imagePath = {});
    void refreshPhotoPreviewPixmap();
    void refreshIdImagePreviewPixmap();
    void syncSideGeometry();

    MemberRepository& m_repository;
    bool m_isEdit = false;
    qint64 m_memberId = 0;
    QString m_photoSourcePath;
    QString m_idImageSourcePath;
    QString m_photoDisplayPath;
    QString m_idImageDisplayPath;
    bool m_photoChanged = false;
    bool m_idImageChanged = false;
    bool m_photoRemoved = false;
    bool m_idImageRemoved = false;
    /// Whether the box shows a real image rather than its placeholder --
    /// what decides between the viewer and the picker on a click.
    bool m_photoShown = false;
    bool m_idImageShown = false;
    bool m_adjustingSide = false;

    QWidget* m_fieldsPanel = nullptr;
    QGridLayout* m_form = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
    QList<FieldLabel> m_fieldLabels;

    QLabel* m_photoPreview = nullptr;
    QLabel* m_idImagePreview = nullptr;

    QLabel* m_numberLabel = nullptr;
    QLineEdit* m_firstNameEdit = nullptr;
    QLineEdit* m_lastNameEdit = nullptr;
    QComboBox* m_sexCombo = nullptr;
    BirthDateEdit* m_dateOfBirthEdit = nullptr;
    QLineEdit* m_emailEdit = nullptr;
    QLineEdit* m_phoneEdit = nullptr;
    QLineEdit* m_addressEdit = nullptr;
    QLineEdit* m_cityEdit = nullptr;
    QLineEdit* m_occupationEdit = nullptr;
    QComboBox* m_statusCombo = nullptr;
    QLabel* m_activeUntilLabel = nullptr;
    /// The stored last active day, empty for a member not yet saved.
    QString m_loadedActiveUntil;
    QPlainTextEdit* m_notesEdit = nullptr;
    QPushButton* m_photoButton = nullptr;
    QPushButton* m_idImageButton = nullptr;
};
