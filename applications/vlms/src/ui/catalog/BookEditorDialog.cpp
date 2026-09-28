#include "ui/catalog/BookEditorDialog.h"

#include <VLMS/Core/DateText.h>
#include <VLMS/Core/Strings.h>
#include <VLMS/Ocr/Ocr.h>
#include "ui/Theme.h"
#include "ui/UiHelpers.h"
#include "QtBridge.h"
#include "ui/catalog/BookCopiesTable.h"
#include "ui/catalog/BookOcrController.h"
#include "ui/catalog/CategoryManagerDialog.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTabWidget>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

using VLMS::T;
using VLMS::cd;
using VLMS::qd;
using VLMS::qs;
using VLMS::qsl;
using VLMS::ss;
using VLMS::svl;

namespace {

constexpr int kSideColumnSpacing = 8;
constexpr int kContentSpacing = 16;
constexpr int kMinCoverHeight = 180;
constexpr int kMinCoverWidth = 120;

const QString kOtherLanguageSentinel = QStringLiteral("__other__");

const QStringList kBookLanguageCodes = {
    QStringLiteral("ar"),
    QStringLiteral("fr"),
    QStringLiteral("en"),
    QStringLiteral("es"),
    QStringLiteral("de"),
    QStringLiteral("it"),
    QStringLiteral("tr"),
    QStringLiteral("pt"),
    QStringLiteral("ru"),
    QStringLiteral("zh"),
};

void setFormLabel(QFormLayout* form, int row, const QString& key)
{
    if (form == nullptr) {
        return;
    }
    if (QLayoutItem* item = form->itemAt(row, QFormLayout::LabelRole)) {
        if (auto* label = qobject_cast<QLabel*>(item->widget())) {
            label->setText(T(ss(key)));
            label->setAlignment(Qt::AlignLeading | Qt::AlignVCenter);
        }
    }
}

QLabel* makeFieldLabel(QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setAlignment(Qt::AlignLeading | Qt::AlignVCenter);
    return label;
}

/** Cover width:height = 2:3 (same as book-placeholder.png). */
QSize coverSizeForHeight(int height)
{
    const int coverHeight = qMax(kMinCoverHeight, height);
    const int coverWidth = qMax(1, (coverHeight * 2) / 3);
    return QSize(coverWidth, coverHeight);
}

QSize coverSizeForWidth(int width)
{
    const int coverWidth = qMax(kMinCoverWidth, width);
    const int coverHeight = qMax(1, (coverWidth * 3) / 2);
    return QSize(coverWidth, coverHeight);
}

QPixmap bookCoverPlaceholder(const QSize& size)
{
    static const QPixmap source(QStringLiteral(":/images/book-placeholder.png"));
    if (source.isNull()) {
        return {};
    }
    // Recoloured after scaling, on the smaller image: the artwork ships in the
    // light palette and would otherwise be a white card on a dark window.
    return VLMS::themedArtwork(
        source.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void showCoverPlaceholder(QLabel* label, const QSize& size)
{
    label->setText({});
    label->setPixmap(bookCoverPlaceholder(size));
}

}  // namespace

BookEditorDialog::BookEditorDialog(VLMS::Repositories::CatalogRepository& repository, QWidget* parent)
    : QDialog(parent),
      m_repository(repository) {
    buildUi();
    populateCategories();
    populateLanguages();
    retranslateUi();
}

BookEditorDialog::BookEditorDialog(VLMS::Repositories::CatalogRepository& repository,
                                   const VLMS::Repositories::BookRecord& book,
                                   QWidget* parent)
    : QDialog(parent),
      m_repository(repository),
      m_isEdit(true),
      m_bookId(book.id) {
    buildUi();
    populateCategories();
    populateLanguages();
    loadBook(book);
    retranslateUi();
}

BookEditorDialog::~BookEditorDialog() = default;

void BookEditorDialog::buildUi() {
    setMinimumSize(980, 600);
    resize(1080, 660);

    auto* root = new QHBoxLayout(this);
    root->setSpacing(kContentSpacing);
    root->setDirection(QBoxLayout::LeftToRight);

    // Left: the two tabs. m_fieldsPanel stays the panel whose height drives
    // syncCoverGeometry(), so wrapping the form in tabs leaves the cover's 2:3
    // sizing exactly as it was -- the tab widget just fills the same box.
    m_fieldsPanel = new QWidget(this);
    auto* panelLayout = new QVBoxLayout(m_fieldsPanel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(0);

    m_tabs = new QTabWidget(m_fieldsPanel);
    panelLayout->addWidget(m_tabs);

    auto* bookTab = new QWidget(m_tabs);
    auto* fieldsColumn = new QVBoxLayout(bookTab);
    fieldsColumn->setContentsMargins(kContentSpacing, kContentSpacing, kContentSpacing, kContentSpacing);
    fieldsColumn->setSpacing(10);

    m_form = new QFormLayout();
    VLMS::configureFormLayout(m_form);
    m_form->setContentsMargins(0, 0, 0, 0);
    m_form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    m_form->setLabelAlignment(Qt::AlignLeading | Qt::AlignVCenter);

    m_titleEdit = new QLineEdit(bookTab);
    m_form->addRow(makeFieldLabel(bookTab), m_titleEdit);

    m_authorEdit = new QLineEdit(bookTab);
    const auto authors = m_repository.listAuthorNames();
    auto* authorCompleter = new QCompleter(authors ? qsl(authors.value()) : QStringList(), this);
    authorCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_authorEdit->setCompleter(authorCompleter);
    m_form->addRow(makeFieldLabel(bookTab), m_authorEdit);

    m_publisherEdit = new QLineEdit(bookTab);
    const auto publishers = m_repository.listPublisherNames();
    auto* publisherCompleter = new QCompleter(publishers ? qsl(publishers.value()) : QStringList(), this);
    publisherCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_publisherEdit->setCompleter(publisherCompleter);
    m_form->addRow(makeFieldLabel(bookTab), m_publisherEdit);

    // The category list is managed from the row that uses it: a librarian who
    // finds the category missing while cataloguing no longer has to abandon the
    // book to add it. The row is one widget, so the form's label column and its
    // RTL mirroring are untouched.
    m_categoryCombo = new QComboBox(bookTab);
    m_categoriesButton = VLMS::makeSecondaryButton({});
    connect(m_categoriesButton, &QPushButton::clicked, this, &BookEditorDialog::manageCategories);

    auto* categoryRow = new QWidget(bookTab);
    auto* categoryLayout = new QHBoxLayout(categoryRow);
    categoryLayout->setContentsMargins(0, 0, 0, 0);
    categoryLayout->setSpacing(8);
    categoryLayout->addWidget(m_categoryCombo, 1);
    categoryLayout->addWidget(m_categoriesButton, 0);
    m_form->addRow(makeFieldLabel(bookTab), categoryRow);

    m_isbnEdit = new QLineEdit(bookTab);
    m_form->addRow(makeFieldLabel(bookTab), m_isbnEdit);

    fieldsColumn->addLayout(m_form);

    // Date/place/pages and language/copies/dimensions share one 6-column grid
    // (label, control) x 3 pairs, so every control lines up between the two rows.
    auto* metaGrid = new QGridLayout();
    metaGrid->setContentsMargins(0, 0, 0, 0);
    metaGrid->setHorizontalSpacing(8);
    metaGrid->setVerticalSpacing(10);
    metaGrid->setColumnStretch(1, 1);
    metaGrid->setColumnStretch(3, 1);
    metaGrid->setColumnStretch(5, 1);

    m_publicationDateEdit = new QLineEdit(bookTab);
    m_publicationDateLabel = makeFieldLabel(bookTab);
    metaGrid->addWidget(m_publicationDateLabel, 0, 0);
    metaGrid->addWidget(m_publicationDateEdit, 0, 1);

    m_placeEdit = new QLineEdit(bookTab);
    m_placeLabel = makeFieldLabel(bookTab);
    metaGrid->addWidget(m_placeLabel, 0, 2);
    metaGrid->addWidget(m_placeEdit, 0, 3);

    m_pagesEdit = new QLineEdit(bookTab);
    m_pagesLabel = makeFieldLabel(bookTab);
    metaGrid->addWidget(m_pagesLabel, 0, 4);
    metaGrid->addWidget(m_pagesEdit, 0, 5);

    auto* languageWidget = new QWidget(bookTab);
    auto* languageLayout = new QVBoxLayout(languageWidget);
    languageLayout->setContentsMargins(0, 0, 0, 0);
    languageLayout->setSpacing(4);
    m_languageCombo = new QComboBox(languageWidget);
    // Named because the Copies tab now also holds combo boxes, one per row, so
    // "the second QComboBox in the dialog" stopped being a way to find this.
    m_languageCombo->setObjectName(QStringLiteral("bookLanguageCombo"));
    m_languageOtherEdit = new QLineEdit(languageWidget);
    m_languageOtherEdit->hide();
    languageLayout->addWidget(m_languageCombo);
    languageLayout->addWidget(m_languageOtherEdit);
    m_languageLabel = makeFieldLabel(bookTab);
    metaGrid->addWidget(m_languageLabel, 1, 0);
    metaGrid->addWidget(languageWidget, 1, 1);

    connect(m_languageCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        const bool isOther = m_languageCombo->currentData().toString() == kOtherLanguageSentinel;
        updateLanguageOtherVisibility();
        if (isOther) {
            m_languageOtherEdit->setFocus();
        }
        // Retarget OCR at the book's new language. Silently skipped once the
        // librarian has picked a language themselves -- see populateOcrLanguages.
        populateOcrLanguages();
        syncCoverGeometry();
    });

    // The copy-count spinner used to sit here. It is gone: holdings are now
    // edited one at a time on the Copies tab, because each one carries the
    // library's own numbering and a count cannot say which copy it means.
    m_dimensionsEdit = new QLineEdit(bookTab);
    m_dimensionsLabel = makeFieldLabel(bookTab);
    metaGrid->addWidget(m_dimensionsLabel, 1, 2);
    metaGrid->addWidget(m_dimensionsEdit, 1, 3);

    fieldsColumn->addLayout(metaGrid);

    auto* descriptionHeader = new QHBoxLayout();
    descriptionHeader->setContentsMargins(0, 0, 0, 0);
    descriptionHeader->setSpacing(8);

    m_descriptionLabel = makeFieldLabel(bookTab);

    // A tool button rather than a push button: the main half starts a run, the
    // arrow opens the language menu. Pressing the button is still one click for
    // the common case where the book's own language is the right one.
    m_ocrButton = new QToolButton(bookTab);
    m_ocrButton->setObjectName(QStringLiteral("btnSecondary"));
    m_ocrButton->setCursor(Qt::PointingHandCursor);
    m_ocrButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_ocrButton->setPopupMode(QToolButton::MenuButtonPopup);
    m_ocrButton->setMenu(new QMenu(m_ocrButton));
    connect(m_ocrButton, &QToolButton::clicked, this, &BookEditorDialog::readDescriptionFromImage);
    m_ocr = new BookOcrController(this, this);
    connect(m_ocr, &BookOcrController::textReady, this, &BookEditorDialog::appendRecognizedText);

    m_coverButton = VLMS::makeSecondaryButton({});
    m_coverButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_coverButton, &QPushButton::clicked, this, &BookEditorDialog::chooseCoverImage);
    m_coverLabel = makeFieldLabel(bookTab);

    descriptionHeader->addWidget(m_descriptionLabel, 0, Qt::AlignVCenter);
    descriptionHeader->addWidget(m_ocrButton, 0, Qt::AlignVCenter);
    descriptionHeader->addWidget(m_coverLabel, 0, Qt::AlignVCenter);
    descriptionHeader->addWidget(m_coverButton, 1, Qt::AlignVCenter);
    fieldsColumn->addLayout(descriptionHeader);

    m_descriptionEdit = new QPlainTextEdit(bookTab);
    m_descriptionEdit->setMinimumHeight(120);
    m_descriptionEdit->setTabChangesFocus(true);
    fieldsColumn->addWidget(m_descriptionEdit, 1);

    m_tabs->addTab(bookTab, QString());

    auto* copiesTab = new QWidget(m_tabs);
    auto* copiesColumn = new QVBoxLayout(copiesTab);
    copiesColumn->setContentsMargins(kContentSpacing, kContentSpacing, kContentSpacing, kContentSpacing);
    copiesColumn->setSpacing(10);
    m_copies = new BookCopiesTable(m_repository, copiesTab);
    connect(m_copies, &BookCopiesTable::addRequested, this, [this]() {
        m_copies->addRow(selectedLanguageCode());
    });
    connect(m_copies, &BookCopiesTable::requestCopiesTab, this, [this]() {
        m_tabs->setCurrentIndex(1);
    });
    copiesColumn->addWidget(m_copies, 1);
    m_tabs->addTab(copiesTab, QString());

    // Right: cover height = fields height - buttons; width from 2:3 ratio.
    auto* sideColumn = new QVBoxLayout();
    sideColumn->setContentsMargins(0, 0, 0, 0);
    sideColumn->setSpacing(kSideColumnSpacing);

    m_coverPreview = new QLabel(this);
    m_coverPreview->setObjectName(QStringLiteral("bookCover"));
    m_coverPreview->setAlignment(Qt::AlignCenter);
    m_coverPreview->setFixedSize(coverSizeForHeight(kMinCoverHeight));
    m_coverPreview->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    updateCoverPreview({});
    sideColumn->addWidget(m_coverPreview, 0, Qt::AlignHCenter);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    VLMS::localizeButtonBox(m_buttonBox);
    m_buttonBox->setCenterButtons(true);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (m_titleEdit->text().trimmed().isEmpty()) {
            VLMS::showWarning(
                this,
                T("book.validation"),
                T("book.titleRequired"));
            return;
        }
        if (selectedLanguageCode().isEmpty()) {
            VLMS::showWarning(
                this,
                T("book.validation"),
                T("book.languageRequired"));
            return;
        }
        // Blanks and duplicates are caught here rather than left to the unique
        // indexes: the librarian gets told which number clashes while the row
        // is still in front of them.
        if (!validateCopies()) {
            return;
        }
        // The field stays free text -- it legitimately holds MARC notation for
        // an uncertain decade -- but what will be stored is shown back before
        // the dialog closes, so 'Nov 06, 1996' becoming 1996-11-06 is something
        // the cataloguer sees rather than discovers later in a list. Anything
        // the normaliser does not understand comes back unchanged.
        m_publicationDateEdit->setText(
            qs(VLMS::Core::DateText::normalizePublicationDate(ss(m_publicationDateEdit->text()))));
        accept();
    });
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    sideColumn->addWidget(m_buttonBox, 0, Qt::AlignHCenter);

    root->addWidget(m_fieldsPanel, 1);
    root->addLayout(sideColumn, 0);
}

void BookEditorDialog::retranslateUi()
{
    setWindowTitle(m_isEdit ? T("book.editTitle")
                            : T("book.addTitle"));

    VLMS::retranslateStandardButtons(m_buttonBox);

    setFormLabel(m_form, 0, QStringLiteral("book.field.title"));
    setFormLabel(m_form, 1, QStringLiteral("book.field.author"));
    setFormLabel(m_form, 2, QStringLiteral("book.field.publisher"));
    setFormLabel(m_form, 3, QStringLiteral("book.field.category"));
    setFormLabel(m_form, 4, QStringLiteral("book.field.isbn"));

    if (m_categoriesButton != nullptr) {
        m_categoriesButton->setText(T("catalog.categories"));
    }

    if (m_publicationDateLabel != nullptr) {
        m_publicationDateLabel->setText(T("book.field.publicationDate"));
    }
    if (m_placeLabel != nullptr) {
        m_placeLabel->setText(T("book.field.place"));
    }
    if (m_pagesLabel != nullptr) {
        m_pagesLabel->setText(T("book.field.pages"));
    }
    if (m_dimensionsLabel != nullptr) {
        m_dimensionsLabel->setText(T("book.field.dimensions"));
    }

    if (m_languageLabel != nullptr) {
        m_languageLabel->setText(T("book.field.language"));
    }
    if (m_tabs != nullptr) {
        m_tabs->setTabText(0, T("book.tab.book"));
        m_tabs->setTabText(1, T("book.tab.copies"));
    }
    if (m_copies != nullptr) {
        m_copies->retranslateUi();
    }
    if (m_coverLabel != nullptr) {
        m_coverLabel->setText(T("book.field.cover"));
    }
    if (m_descriptionLabel != nullptr) {
        m_descriptionLabel->setText(T("book.field.description"));
    }
    if (m_ocrButton != nullptr) {
        m_ocrButton->setText(T("ocr.readFromImage"));
        m_ocrButton->setEnabled(VLMS::Ocr::isAvailable());
        // Rebuilt rather than relabelled: the menu's entries are language names
        // and have to follow the locale like everything else here.
        populateOcrLanguages();
    }

    m_publicationDateEdit->setPlaceholderText(T("book.field.publicationDateHint"));
    populateLanguages();

    if (!m_coverChanged && m_coverButton->text().isEmpty()) {
        m_coverButton->setText(T("book.chooseCover"));
    }

    populateCategories();
}

void BookEditorDialog::manageCategories() {
    CategoryManagerDialog dialog(m_repository, this);
    dialog.exec();
    // The list the combo shows may have grown or lost an entry; the current
    // pick is read and restored by populateCategories itself.
    populateCategories();
}

void BookEditorDialog::populateCategories() {
    const int currentId = m_categoryCombo->currentData().toLongLong();
    m_categoryCombo->clear();
    m_categoryCombo->addItem(T("book.noneCategory"), 0);

    const auto categories = m_repository.listAllCategories();
    if (!categories) {
        VLMS::showRepoError(this, categories.error());
        return;
    }
    for (const VLMS::Repositories::CategoryRecord& category : categories.value()) {
        const QString label = qs(category.label.empty() ? category.code : category.label);
        m_categoryCombo->addItem(QStringLiteral("%1 (%2)").arg(label, qs(category.code)),
                                 QVariant::fromValue(category.id));
    }

    const int index = m_categoryCombo->findData(currentId);
    if (index >= 0) {
        m_categoryCombo->setCurrentIndex(index);
    }
}

void BookEditorDialog::populateLanguages() {
    const QString currentLanguage = selectedLanguageCode();

    m_languageCombo->blockSignals(true);
    m_languageCombo->clear();
    for (const QString& code : kBookLanguageCodes) {
        m_languageCombo->addItem(T(ss(QStringLiteral("book.language.%1").arg(code))), code);
    }
    m_languageCombo->addItem(T("book.language.other"), kOtherLanguageSentinel);
    setLanguageSelection(currentLanguage);
    m_languageCombo->blockSignals(false);
    updateLanguageOtherVisibility();
}

void BookEditorDialog::setLanguageSelection(const QString& language) {
    const QString trimmed = language.trimmed();
    const int presetIndex = m_languageCombo->findData(trimmed);
    if (presetIndex >= 0) {
        m_languageCombo->setCurrentIndex(presetIndex);
        m_languageOtherEdit->clear();
        return;
    }

    if (!trimmed.isEmpty()) {
        const int otherIndex = m_languageCombo->findData(kOtherLanguageSentinel);
        if (otherIndex >= 0) {
            m_languageCombo->setCurrentIndex(otherIndex);
            m_languageOtherEdit->setText(trimmed);
            return;
        }
    }

    if (m_languageCombo->count() > 0) {
        m_languageCombo->setCurrentIndex(0);
        m_languageOtherEdit->clear();
    }
    updateLanguageOtherVisibility();
}

void BookEditorDialog::updateLanguageOtherVisibility() {
    const bool isOther = m_languageCombo->currentData().toString() == kOtherLanguageSentinel;
    m_languageOtherEdit->setVisible(isOther);
    if (isOther) {
        m_languageOtherEdit->setPlaceholderText(T("book.field.languageOtherHint"));
    }
}

QString BookEditorDialog::selectedLanguageCode() const {
    const QString data = m_languageCombo->currentData().toString();
    if (data == kOtherLanguageSentinel) {
        return m_languageOtherEdit->text().trimmed();
    }
    return data;
}

void BookEditorDialog::chooseCoverImage()
{
    const QString path = VLMS::askForImageFile(
        this,
        T("book.selectCover"),
        T("book.coverFilter"));
    if (path.isEmpty()) {
        return;
    }
    m_coverSourcePath = path;
    m_coverChanged = true;
    m_coverButton->setText(QFileInfo(path).fileName());
    updateCoverPreview(path);
}

void BookEditorDialog::updateCoverPreview(const QString& imagePath)
{
    m_coverDisplayPath = imagePath.trimmed();
    refreshCoverPreviewPixmap();
}

QSize BookEditorDialog::currentCoverSize() const
{
    if (m_coverPreview != nullptr && m_coverPreview->width() > 0 && m_coverPreview->height() > 0) {
        return m_coverPreview->size();
    }
    return coverSizeForHeight(kMinCoverHeight);
}

void BookEditorDialog::refreshCoverPreviewPixmap()
{
    if (m_coverPreview == nullptr) {
        return;
    }

    const QSize coverSize = currentCoverSize();
    if (m_coverDisplayPath.isEmpty()) {
        showCoverPlaceholder(m_coverPreview, coverSize);
        return;
    }

    const QPixmap pixmap(m_coverDisplayPath);
    if (pixmap.isNull()) {
        showCoverPlaceholder(m_coverPreview, coverSize);
        return;
    }

    m_coverPreview->setText({});
    m_coverPreview->setPixmap(pixmap.scaled(coverSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void BookEditorDialog::syncCoverGeometry()
{
    if (m_adjustingCover || m_fieldsPanel == nullptr || m_coverPreview == nullptr || m_buttonBox == nullptr) {
        return;
    }

    const int fieldsHeight = m_fieldsPanel->height();
    if (fieldsHeight <= 0) {
        return;
    }

    const int buttonHeight = qMax(m_buttonBox->height(), m_buttonBox->sizeHint().height());
    const int availableHeight = fieldsHeight - buttonHeight - kSideColumnSpacing;

    QSize coverSize = coverSizeForWidth(width() / 3);
    if (coverSize.height() > availableHeight) {
        coverSize = coverSizeForHeight(availableHeight);
    }

    m_adjustingCover = true;

    if (m_coverPreview->size() != coverSize) {
        m_coverPreview->setFixedSize(coverSize);
        refreshCoverPreviewPixmap();
    }

    m_adjustingCover = false;
}

void BookEditorDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    syncCoverGeometry();
}

void BookEditorDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    syncCoverGeometry();
}

void BookEditorDialog::populateOcrLanguages()
{
    QMenu* menu = m_ocrButton != nullptr ? m_ocrButton->menu() : nullptr;
    if (menu == nullptr) {
        return;
    }
    menu->clear();

    const std::vector<std::string> installed = VLMS::Ocr::availableLanguages();
    if (installed.empty()) {
        m_ocrLanguages.clear();
        return;
    }

    for (const std::string& code : installed) {
        const QString tesseractCode = QString::fromStdString(code);

        // Label with the catalogue's own name for the language, so the menu
        // reads "عربي / Arabe / Arabic" rather than "ara". An installed model
        // this module has no mapping for still gets an entry, under its raw
        // code -- better a slightly technical label than a hidden capability.
        const QString isoCode =
            QString::fromStdString(VLMS::Ocr::bookLanguageForTesseractCode(code));
        const QString label =
            isoCode.isEmpty() ? tesseractCode : qs(VLMS::Core::Strings::bookLanguageLabel(ss(isoCode)));

        QAction* action = menu->addAction(label);
        action->setCheckable(true);
        action->setData(tesseractCode);
        connect(action, &QAction::toggled, this, [this, tesseractCode](bool checked) {
            // Any change here is the librarian's, so stop following the book's
            // language for the rest of this dialog.
            m_ocrLanguageChosenByUser = true;
            if (checked) {
                if (!m_ocrLanguages.contains(tesseractCode)) {
                    m_ocrLanguages.append(tesseractCode);
                }
            } else {
                m_ocrLanguages.removeAll(tesseractCode);
            }
            // Unchecking the last one would mean "recognise nothing"; keep it.
            if (m_ocrLanguages.isEmpty()) {
                m_ocrLanguages.append(tesseractCode);
            }
            updateOcrLanguageMenuState();
        });
    }

    if (!m_ocrLanguageChosenByUser) {
        const std::string preferred =
            VLMS::Ocr::defaultLanguageForBook(selectedLanguageCode().toStdString());
        m_ocrLanguages.clear();
        if (!preferred.empty()) {
            m_ocrLanguages.append(QString::fromStdString(preferred));
        }
    }

    updateOcrLanguageMenuState();
}

void BookEditorDialog::updateOcrLanguageMenuState()
{
    QMenu* menu = m_ocrButton != nullptr ? m_ocrButton->menu() : nullptr;
    if (menu == nullptr) {
        return;
    }

    const QList<QAction*> actions = menu->actions();
    for (QAction* action : actions) {
        const QSignalBlocker blocker(action);
        action->setChecked(m_ocrLanguages.contains(action->data().toString()));
    }

    // The tooltip is where the current choice is actually legible: the button
    // face has to stay short, and a librarian who wonders "which language is
    // this about to use?" should not have to open the menu to find out.
    if (m_ocrButton != nullptr) {
        QStringList labels;
        for (const QString& code : m_ocrLanguages) {
            const QString iso =
                QString::fromStdString(VLMS::Ocr::bookLanguageForTesseractCode(
                    code.toStdString()));
            labels.append(iso.isEmpty() ? code : qs(VLMS::Core::Strings::bookLanguageLabel(ss(iso))));
        }
        m_ocrButton->setToolTip(
            T("ocr.readFromImageTip")
            + QStringLiteral("\n")
            + T("ocr.languageInUse", "languages", ss(labels.join(QStringLiteral(" + ")))));
    }
}

QString BookEditorDialog::selectedOcrLanguages() const
{
    return m_ocrLanguages.join(QLatin1Char('+'));
}

void BookEditorDialog::readDescriptionFromImage()
{
    m_ocr->start(selectedOcrLanguages());
}


void BookEditorDialog::appendRecognizedText(const QString& text)
{
    const QString existing = m_descriptionEdit->toPlainText().trimmed();
    if (existing.isEmpty()) {
        m_descriptionEdit->setPlainText(text);
    } else {
        m_descriptionEdit->setPlainText(existing + QStringLiteral("\n\n") + text);
    }
}

void BookEditorDialog::loadBook(const VLMS::Repositories::BookRecord& book) {
    m_titleEdit->setText(qs(book.title));
    m_authorEdit->setText(qs(book.authorName));
    m_publisherEdit->setText(qs(book.publisherName));
    m_isbnEdit->setText(qs(book.isbn));
    m_publicationDateEdit->setText(qs(book.publicationDate));
    m_placeEdit->setText(qs(book.placeOfPublication));
    m_pagesEdit->setText(qs(book.pages));
    m_dimensionsEdit->setText(qs(book.dimensions));
    m_descriptionEdit->setPlainText(qs(book.description));

    setLanguageSelection(qs(book.language));

    const int categoryIndex = m_categoryCombo->findData(QVariant::fromValue(book.categoryId));
    if (categoryIndex >= 0) {
        m_categoryCombo->setCurrentIndex(categoryIndex);
    }

    loadCopies(book.id);

    if (!book.coverImagePath.empty()) {
        const QString coverPath = qs(m_repository.resolveCoverPath(book.coverImagePath));
        m_coverButton->setText(QFileInfo(coverPath).fileName());
        updateCoverPreview(coverPath);
    } else {
        updateCoverPreview({});
    }
}

VLMS::Repositories::BookInput BookEditorDialog::bookInput() const {
    VLMS::Repositories::BookInput input;
    input.title = ss(m_titleEdit->text());
    input.authorName = ss(m_authorEdit->text());
    input.publisherName = ss(m_publisherEdit->text());
    input.categoryId = m_categoryCombo->currentData().toLongLong();
    input.isbn = ss(m_isbnEdit->text());
    input.publicationDate = ss(m_publicationDateEdit->text());
    input.placeOfPublication = ss(m_placeEdit->text());
    input.pages = ss(m_pagesEdit->text());
    input.dimensions = ss(m_dimensionsEdit->text());
    input.language = ss(selectedLanguageCode());
    input.description = ss(m_descriptionEdit->toPlainText());
    // Only meaningful when adding: createBook auto-generates this many copies.
    // If the librarian filled the Copies tab in, those rows replace them --
    // asking for the same number keeps the two from stacking up.
    input.initialCopyCount = qMax(1, m_copies->copyCount());
    return input;
}

std::vector<VLMS::Repositories::BookCopyInput> BookEditorDialog::copyInputs() const {
    return m_copies->copyInputs();
}

void BookEditorDialog::reserveCopyNumber(const VLMS::Repositories::BookCopyRecord& archivedCopy)
{
    m_copies->addReservedRow(qs(archivedCopy.source), qs(archivedCopy.localId),
                             qs(archivedCopy.globalCopyId));
}

void BookEditorDialog::loadCopies(const qint64 bookId) {
    m_copies->loadCopies(bookId);
}

bool BookEditorDialog::validateCopies() {
    return m_copies->validate(this);
}

