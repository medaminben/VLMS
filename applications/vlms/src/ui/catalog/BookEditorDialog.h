#pragma once

#include <VLMS/Core/CatalogRepository.h>
#include <VLMS/Core/CatalogTypes.h>

#include <QDialog>
#include <QString>
#include <QStringList>

class BookCopiesTable;
class BookOcrController;
class QComboBox;
class QDialogButtonBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QTabWidget;
class QToolButton;
class QWidget;

class BookEditorDialog final : public QDialog {
    Q_OBJECT

public:
    BookEditorDialog(CatalogRepository& repository, QWidget* parent = nullptr);
    BookEditorDialog(CatalogRepository& repository, const BookRecord& book, QWidget* parent = nullptr);
    ~BookEditorDialog() override;

    [[nodiscard]] BookInput bookInput() const;

    /// The copies exactly as the table now shows them, for
    /// CatalogRepository::saveCopies. Rows the librarian deleted are simply
    /// absent -- that is what makes the list the whole truth.
    [[nodiscard]] std::vector<BookCopyInput> copyInputs() const;

    /// Archive -> Reuse: adds a locked copy row carrying the archived copy's
    /// number. The archived copy keeps it until the save commits.
    void reserveCopyNumber(const BookCopyRecord& archivedCopy);

    [[nodiscard]] QString coverSourcePath() const { return m_coverSourcePath; }
    [[nodiscard]] bool coverChanged() const { return m_coverChanged; }

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void retranslateUi();
    void populateCategories();
    void manageCategories();
    void populateLanguages();
    void loadBook(const BookRecord& book);

    void loadCopies(qint64 bookId);
    [[nodiscard]] bool validateCopies();

    void setLanguageSelection(const QString& language);
    void updateLanguageOtherVisibility();
    void readDescriptionFromImage();

    /// Rebuilds the OCR language menu from what is installed, and preselects
    /// the book's own language -- unless the librarian has already overridden
    /// it for this dialog, in which case their choice wins.
    void populateOcrLanguages();
    void updateOcrLanguageMenuState();
    [[nodiscard]] QString selectedOcrLanguages() const;

    void appendRecognizedText(const QString& text);

    void chooseCoverImage();
    void updateCoverPreview(const QString& imagePath = {});
    void refreshCoverPreviewPixmap();
    void syncCoverGeometry();
    [[nodiscard]] QString selectedLanguageCode() const;
    [[nodiscard]] QSize currentCoverSize() const;

    CatalogRepository& m_repository;
    bool m_isEdit = false;
    qint64 m_bookId = 0;
    QString m_coverSourcePath;
    QString m_coverDisplayPath;
    bool m_coverChanged = false;
    bool m_adjustingCover = false;

    QWidget* m_fieldsPanel = nullptr;
    QTabWidget* m_tabs = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
    QFormLayout* m_form = nullptr;

    QLabel* m_coverPreview = nullptr;
    QLabel* m_languageLabel = nullptr;
    QLabel* m_coverLabel = nullptr;
    QLabel* m_descriptionLabel = nullptr;
    QLabel* m_publicationDateLabel = nullptr;
    QLabel* m_placeLabel = nullptr;
    QLabel* m_pagesLabel = nullptr;
    QLabel* m_dimensionsLabel = nullptr;

    QLineEdit* m_titleEdit = nullptr;
    QLineEdit* m_authorEdit = nullptr;
    QLineEdit* m_publisherEdit = nullptr;
    QComboBox* m_categoryCombo = nullptr;
    QPushButton* m_categoriesButton = nullptr;
    QLineEdit* m_isbnEdit = nullptr;
    QLineEdit* m_publicationDateEdit = nullptr;
    QLineEdit* m_placeEdit = nullptr;
    QLineEdit* m_pagesEdit = nullptr;
    QLineEdit* m_dimensionsEdit = nullptr;
    QComboBox* m_languageCombo = nullptr;
    QLineEdit* m_languageOtherEdit = nullptr;
    QPushButton* m_coverButton = nullptr;
    QPlainTextEdit* m_descriptionEdit = nullptr;

    BookCopiesTable* m_copies = nullptr;

    // OCR. The button carries a dropdown of the installed languages, so the
    // librarian can override the book's own language for a single run -- a
    // French preface in an Arabic book, a bilingual title page.
    QToolButton* m_ocrButton = nullptr;
    QStringList m_ocrLanguages;             ///< Tesseract codes, joined with '+'
    bool m_ocrLanguageChosenByUser = false; ///< stop tracking the book's language

    BookOcrController* m_ocr = nullptr;
};
