#include "ui/members/MemberEditorDialog.h"

#include "ui/members/BirthDateEdit.h"
#include "ui/ImageViewerDialog.h"

#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Strings.h>
#include "ui/UiHelpers.h"
#include "QtBridge.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <utility>

using VLMS::T;
using VLMS::cd;
using VLMS::qd;
using VLMS::qs;
using VLMS::qsl;
using VLMS::ss;
using VLMS::svl;

namespace {

using VLMS::Strings;

constexpr int kSideColumnSpacing = 8;
constexpr int kContentSpacing = 16;
constexpr int kPreviewWidth = 220;
constexpr int kMinPhotoHeight = 140;
constexpr int kMinIdImageHeight = 100;

/// The four grid columns: a label and its control, twice across.
constexpr int kLabelColumn = 0;
constexpr int kFieldColumn = 1;
constexpr int kSecondLabelColumn = 2;
constexpr int kSecondFieldColumn = 3;
constexpr int kColumnCount = 4;

void showImagePlaceholder(QLabel* label, const QString& text, const QSize& size)
{
    label->setPixmap({});
    label->setText(text);
    label->setWordWrap(true);
    if (size.isValid()) {
        label->setFixedSize(size);
    }
}

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

}  // namespace

MemberEditorDialog::MemberEditorDialog(MemberRepository& repository, QWidget* parent)
    : QDialog(parent),
      m_repository(repository) {
    buildUi();
    populateStatuses();
    populateSexes();
    m_numberLabel->setText(qs(m_repository.suggestMembershipNumber()));
    retranslateUi();
    connect(m_statusCombo, &QComboBox::currentIndexChanged,
            this, &MemberEditorDialog::updateActiveUntil);
    updateActiveUntil();
}

MemberEditorDialog::MemberEditorDialog(MemberRepository& repository,
                                       const MemberRecord& member,
                                       QWidget* parent)
    : QDialog(parent),
      m_repository(repository),
      m_isEdit(true),
      m_memberId(member.id) {
    buildUi();
    populateStatuses();
    populateSexes();
    loadMember(member);
    retranslateUi();
    connect(m_statusCombo, &QComboBox::currentIndexChanged,
            this, &MemberEditorDialog::updateActiveUntil);
    updateActiveUntil();
}

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

QLabel* MemberEditorDialog::addFieldLabel(const QString& key) {
    auto* label = new QLabel(m_fieldsPanel);
    label->setAlignment(Qt::AlignLeading | Qt::AlignVCenter);
    m_fieldLabels.push_back({label, key});
    return label;
}

void MemberEditorDialog::addWideRow(int row, const QString& labelKey, QWidget* field) {
    m_form->addWidget(addFieldLabel(labelKey), row, kLabelColumn);
    m_form->addWidget(field, row, kFieldColumn, 1, kColumnCount - kFieldColumn);
}

void MemberEditorDialog::addPairedRow(int row,
                                      const QString& labelKey,
                                      QWidget* field,
                                      const QString& secondLabelKey,
                                      QWidget* secondField) {
    m_form->addWidget(addFieldLabel(labelKey), row, kLabelColumn);
    m_form->addWidget(field, row, kFieldColumn);
    m_form->addWidget(addFieldLabel(secondLabelKey), row, kSecondLabelColumn);
    m_form->addWidget(secondField, row, kSecondFieldColumn);
}

void MemberEditorDialog::buildUi() {
    setMinimumSize(800, 500);
    resize(900, 560);

    auto* root = new QHBoxLayout(this);
    root->setSpacing(kContentSpacing);
    root->setDirection(QBoxLayout::LeftToRight);

    m_fieldsPanel = new QWidget(this);
    auto* fieldsColumn = new QVBoxLayout(m_fieldsPanel);
    fieldsColumn->setContentsMargins(0, 0, 0, 0);
    fieldsColumn->setSpacing(10);

    // A grid rather than a form layout: half of these lines carry a second
    // label/control pair, and only a real second column keeps Status, Sex and
    // City on one vertical line down the dialog.
    m_form = new QGridLayout();
    m_form->setContentsMargins(16, 16, 16, 16);
    m_form->setHorizontalSpacing(kContentSpacing);
    m_form->setVerticalSpacing(10);
    m_form->setColumnStretch(kFieldColumn, 1);
    m_form->setColumnStretch(kSecondFieldColumn, 1);

    int row = 0;

    m_numberLabel = new QLabel(m_fieldsPanel);
    m_numberLabel->setObjectName(QStringLiteral("memberNumberValue"));
    m_numberLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_statusCombo = new QComboBox(m_fieldsPanel);
    m_statusCombo->setObjectName(QStringLiteral("memberStatusCombo"));
    addPairedRow(row++,
                 QStringLiteral("member.field.number"),
                 m_numberLabel,
                 QStringLiteral("member.field.status"),
                 m_statusCombo);

    m_activeUntilLabel = new QLabel(m_fieldsPanel);
    m_activeUntilLabel->setObjectName(QStringLiteral("memberActiveUntilValue"));
    m_activeUntilLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    addWideRow(row++, QStringLiteral("member.field.activeUntil"), m_activeUntilLabel);

    m_firstNameEdit = new QLineEdit(m_fieldsPanel);
    addWideRow(row++, QStringLiteral("member.field.firstName"), m_firstNameEdit);

    m_lastNameEdit = new QLineEdit(m_fieldsPanel);
    addWideRow(row++, QStringLiteral("member.field.lastName"), m_lastNameEdit);

    m_dateOfBirthEdit = new BirthDateEdit(m_fieldsPanel);
    m_sexCombo = new QComboBox(m_fieldsPanel);
    addPairedRow(row++,
                 QStringLiteral("member.field.dateOfBirth"),
                 m_dateOfBirthEdit,
                 QStringLiteral("member.field.sex"),
                 m_sexCombo);

    m_occupationEdit = new QLineEdit(m_fieldsPanel);
    addWideRow(row++, QStringLiteral("member.field.occupation"), m_occupationEdit);

    m_emailEdit = new QLineEdit(m_fieldsPanel);
    // RFC 5321's ceiling for a whole address. The repository rejects anything
    // longer anyway; stopping it at the keyboard is friendlier than a warning.
    m_emailEdit->setMaxLength(254);
    addWideRow(row++, QStringLiteral("member.field.email"), m_emailEdit);

    m_phoneEdit = new QLineEdit(m_fieldsPanel);
    addWideRow(row++, QStringLiteral("member.field.phone"), m_phoneEdit);

    m_addressEdit = new QLineEdit(m_fieldsPanel);
    m_cityEdit = new QLineEdit(m_fieldsPanel);
    addPairedRow(row++,
                 QStringLiteral("member.field.address"),
                 m_addressEdit,
                 QStringLiteral("member.field.city"),
                 m_cityEdit);

    m_photoButton = VLMS::makeSecondaryButton({});
    m_photoButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_photoButton, &QPushButton::clicked, this, &MemberEditorDialog::choosePhotoImage);

    m_idImageButton = VLMS::makeSecondaryButton({});
    m_idImageButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_idImageButton, &QPushButton::clicked, this, &MemberEditorDialog::chooseIdImage);

    addPairedRow(row++,
                 QStringLiteral("member.field.photo"),
                 m_photoButton,
                 QStringLiteral("member.field.idImage"),
                 m_idImageButton);

    // Notes is the one field wide enough to want its label above it rather
    // than beside it, so both take the full width of the grid.
    m_form->addWidget(addFieldLabel(QStringLiteral("member.field.notes")),
                      row++,
                      kLabelColumn,
                      1,
                      kColumnCount);

    m_notesEdit = new QPlainTextEdit(m_fieldsPanel);
    m_notesEdit->setMinimumHeight(120);
    m_notesEdit->setTabChangesFocus(true);
    m_form->addWidget(m_notesEdit, row, kLabelColumn, 1, kColumnCount);
    m_form->setRowStretch(row, 1);

    fieldsColumn->addLayout(m_form, 1);

    auto* sideColumn = new QVBoxLayout();
    sideColumn->setContentsMargins(0, 0, 0, 0);
    sideColumn->setSpacing(kSideColumnSpacing);

    m_photoPreview = new QLabel(this);
    m_photoPreview->setObjectName(QStringLiteral("memberPhoto"));
    m_photoPreview->setAlignment(Qt::AlignCenter);
    m_photoPreview->setFixedSize(kPreviewWidth, kMinPhotoHeight);
    m_photoPreview->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_photoPreview->setCursor(Qt::PointingHandCursor);
    m_photoPreview->installEventFilter(this);
    updatePhotoPreview({});
    sideColumn->addWidget(m_photoPreview, 0, Qt::AlignHCenter);

    m_idImagePreview = new QLabel(this);
    m_idImagePreview->setObjectName(QStringLiteral("memberIdImage"));
    m_idImagePreview->setAlignment(Qt::AlignCenter);
    m_idImagePreview->setFixedSize(kPreviewWidth, kMinIdImageHeight);
    m_idImagePreview->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_idImagePreview->setCursor(Qt::PointingHandCursor);
    m_idImagePreview->installEventFilter(this);
    updateIdImagePreview({});
    sideColumn->addWidget(m_idImagePreview, 1, Qt::AlignHCenter);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->setCenterButtons(true);
    VLMS::localizeButtonBox(m_buttonBox);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        const ValidationFailure failure = firstValidationFailure();
        if (failure.messageKey.isEmpty()) {
            accept();
            return;
        }

        VLMS::showWarning(this,
                                T("member.validation"),
                                T(ss(failure.messageKey)));
        if (failure.field != nullptr) {
            failure.field->setFocus();
        }
    });
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    sideColumn->addWidget(m_buttonBox, 0, Qt::AlignHCenter);

    root->addWidget(m_fieldsPanel, 1);
    root->addLayout(sideColumn, 0);
}

MemberEditorDialog::ValidationFailure MemberEditorDialog::firstValidationFailure() const {
    if (m_firstNameEdit->text().trimmed().isEmpty()) {
        return {QStringLiteral("member.nameRequired"), m_firstNameEdit};
    }
    if (m_lastNameEdit->text().trimmed().isEmpty()) {
        return {QStringLiteral("member.nameRequired"), m_lastNameEdit};
    }
    // The repository refuses a date it cannot parse too, but that refusal
    // arrives after the dialog has closed: the librarian gets an error box
    // over the members list, with the whole form and its typo already gone.
    // Same rule, one step earlier, so the cost of a bad date is a keystroke.
    QComboBox* const day = m_dateOfBirthEdit->dayCombo();
    QComboBox* const month = m_dateOfBirthEdit->monthCombo();
    QComboBox* const year = m_dateOfBirthEdit->yearCombo();
    if (day->currentIndex() < 0) {
        return {QStringLiteral("member.dateOfBirthRequired"), day};
    }
    if (month->currentIndex() < 0) {
        return {QStringLiteral("member.dateOfBirthRequired"), month};
    }
    if (year->currentIndex() < 0) {
        return {QStringLiteral("member.dateOfBirthRequired"), year};
    }
    const QString iso = m_dateOfBirthEdit->isoDate();
    if (!MemberRepository::isValidDateOfBirth(ss(iso))) {
        return {QStringLiteral("member.dateOfBirthInvalid"), day};
    }
    if (MemberRepository::isDateOfBirthInFuture(ss(iso))) {
        return {QStringLiteral("member.dateOfBirthInFuture"), year};
    }
    if (!MemberRepository::isValidEmail(ss(m_emailEdit->text()))) {
        return {QStringLiteral("member.emailInvalid"), m_emailEdit};
    }
    return {};
}

void MemberEditorDialog::retranslateUi() {
    setWindowTitle(m_isEdit ? T("member.editTitle")
                            : T("member.addTitle"));

    VLMS::retranslateStandardButtons(m_buttonBox);

    for (const FieldLabel& field : m_fieldLabels) {
        field.label->setText(T(ss(field.key)));
    }

    m_numberLabel->setToolTip(T("member.field.numberHint"));

    populateStatuses();
    populateSexes();

    if (!m_photoChanged && m_photoButton->text().isEmpty()) {
        m_photoButton->setText(T("member.choosePhoto"));
    }
    if (!m_idImageChanged && m_idImageButton->text().isEmpty()) {
        m_idImageButton->setText(T("member.chooseIdImage"));
    }

    refreshPhotoPreviewPixmap();
    refreshIdImagePreviewPixmap();
    growToLayoutMinimum();
}

void MemberEditorDialog::growToLayoutMinimum() {
    // An explicit setMinimumSize outranks the layout's own minimum, and the
    // form needs more than 900x560: about 590px tall in every language, wider
    // still in French. Squeezed, the grid cut the bottom off the birth-date
    // boxes and slid the Sex label over the year, so grow to what it needs.
    // Called again on show: the Arabic rows gain a pixel once polished.
    layout()->activate();
    const QSize needed = layout()->minimumSize();
    setMinimumSize(minimumSize().expandedTo(needed));
    resize(size().expandedTo(needed));
}

void MemberEditorDialog::populateStatuses() {
    const QString currentStatus = m_statusCombo->currentData().toString();
    m_statusCombo->clear();

    for (const std::string& code : MemberRepository::statusCodes()) {
        m_statusCombo->addItem(qs(Strings::memberStatusLabel(code)), qs(code));
    }

    const QString target =
        currentStatus.isEmpty() ? QString::fromLatin1(MemberStatus::kActive) : currentStatus;
    const int index = m_statusCombo->findData(target);
    if (index >= 0) {
        m_statusCombo->setCurrentIndex(index);
    } else if (m_statusCombo->count() > 0) {
        m_statusCombo->setCurrentIndex(0);
    }
}

void MemberEditorDialog::populateSexes() {
    const QString currentSex = m_sexCombo->currentData().toString();
    m_sexCombo->clear();

    for (const std::string& code : MemberRepository::sexCodes()) {
        m_sexCombo->addItem(qs(Strings::memberSexLabel(code)), qs(code));
    }

    const int index = m_sexCombo->findData(currentSex);
    if (index >= 0) {
        m_sexCombo->setCurrentIndex(index);
    } else if (m_sexCombo->count() > 0) {
        m_sexCombo->setCurrentIndex(0);
    }
}

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

void MemberEditorDialog::updatePhotoPreview(const QString& imagePath)
{
    m_photoDisplayPath = imagePath.trimmed();
    refreshPhotoPreviewPixmap();
}

void MemberEditorDialog::updateIdImagePreview(const QString& imagePath)
{
    m_idImageDisplayPath = imagePath.trimmed();
    refreshIdImagePreviewPixmap();
}

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

void MemberEditorDialog::syncSideGeometry()
{
    if (m_adjustingSide || m_fieldsPanel == nullptr || m_photoPreview == nullptr
        || m_idImagePreview == nullptr || m_buttonBox == nullptr) {
        return;
    }

    const int fieldsHeight = m_fieldsPanel->height();
    if (fieldsHeight <= 0) {
        return;
    }

    const int buttonHeight = qMax(m_buttonBox->height(), m_buttonBox->sizeHint().height());
    const int gaps = kSideColumnSpacing * 2;
    const int available = fieldsHeight - buttonHeight - gaps;
    const int photoHeight = qMax(kMinPhotoHeight, (available * 55) / 100);
    const int idHeight = qMax(kMinIdImageHeight, available - photoHeight);

    m_adjustingSide = true;

    if (m_photoPreview->size() != QSize(kPreviewWidth, photoHeight)) {
        m_photoPreview->setFixedSize(kPreviewWidth, photoHeight);
        refreshPhotoPreviewPixmap();
    }
    if (m_idImagePreview->size() != QSize(kPreviewWidth, idHeight)) {
        m_idImagePreview->setFixedSize(kPreviewWidth, idHeight);
        refreshIdImagePreviewPixmap();
    }

    m_adjustingSide = false;
}

void MemberEditorDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    growToLayoutMinimum();
    syncSideGeometry();
}

void MemberEditorDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    syncSideGeometry();
}

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

void MemberEditorDialog::updateActiveUntil() {
    // The date the save will store, so the librarian sees what Active buys.
    const std::string chosen = ss(m_statusCombo->currentData().toString());
    m_activeUntilLabel->setText(qs(MemberRepository::activeUntilFor(
        ss(m_loadedActiveUntil), chosen, VLMS::Clock::today())));
}

void MemberEditorDialog::loadMember(const MemberRecord& member) {
    m_numberLabel->setText(qs(member.membershipNumber));
    m_firstNameEdit->setText(qs(member.firstName));
    m_lastNameEdit->setText(qs(member.lastName));

    const int sexIndex = m_sexCombo->findData(qs(member.sex));
    if (sexIndex >= 0) {
        m_sexCombo->setCurrentIndex(sexIndex);
    } else if (m_sexCombo->count() > 0) {
        m_sexCombo->setCurrentIndex(0);
    }

    m_dateOfBirthEdit->setIsoDate(qs(member.dateOfBirth));
    m_emailEdit->setText(qs(member.email));
    m_phoneEdit->setText(qs(member.phone));
    m_addressEdit->setText(qs(member.address));
    m_cityEdit->setText(qs(member.city));
    m_occupationEdit->setText(qs(member.occupation));
    m_notesEdit->setPlainText(qs(member.notes));

    m_loadedActiveUntil = qs(member.activeUntil);
    const int statusIndex = m_statusCombo->findData(qs(member.status));
    if (statusIndex >= 0) {
        m_statusCombo->setCurrentIndex(statusIndex);
    }

    if (!member.photoPath.empty()) {
        const QString photoPath = qs(m_repository.resolveImagePath(member.photoPath));
        m_photoButton->setText(QFileInfo(photoPath).fileName());
        updatePhotoPreview(photoPath);
    } else {
        updatePhotoPreview({});
    }

    if (!member.idImagePath.empty()) {
        const QString idPath = qs(m_repository.resolveImagePath(member.idImagePath));
        m_idImageButton->setText(QFileInfo(idPath).fileName());
        updateIdImagePreview(idPath);
    } else {
        updateIdImagePreview({});
    }
}

MemberInput MemberEditorDialog::memberInput() const {
    MemberInput input;
    input.firstName = ss(m_firstNameEdit->text());
    input.lastName = ss(m_lastNameEdit->text());
    input.sex = ss(m_sexCombo->currentData().toString());
    input.dateOfBirth = ss(m_dateOfBirthEdit->isoDate());
    input.email = ss(m_emailEdit->text());
    input.phone = ss(m_phoneEdit->text());
    input.address = ss(m_addressEdit->text());
    input.city = ss(m_cityEdit->text());
    input.occupation = ss(m_occupationEdit->text());
    input.status = ss(m_statusCombo->currentData().toString());
    input.notes = ss(m_notesEdit->toPlainText());
    const QString composed = (m_firstNameEdit->text().trimmed() + QLatin1Char(' ')
                              + m_lastNameEdit->text().trimmed())
                                 .trimmed();
    input.fullName = ss(composed);
    return input;
}
