#include "ui/circulation/LoanCheckoutDialog.h"

#include <VLMS/Core/Clock.h>
#include <VLMS/Repositories/LoanPolicy.h>
#include <VLMS/Core/Strings.h>
#include "ui/UiHelpers.h"
#include "QtBridge.h"

#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVariant>
#include <QVBoxLayout>

using VLMS::T;
using VLMS::cd;
using VLMS::qd;
using VLMS::qs;
using VLMS::qsl;
using VLMS::ss;
using VLMS::svl;

namespace {

using VLMS::Clock;
using VLMS::Strings;

QString memberOptionLabel(const VLMS::Repositories::LoanMemberOption& member) {
    return QStringLiteral("%1 — %2 %3")
        .arg(qs(member.membershipNumber), qs(member.firstName), qs(member.lastName))
        .trimmed();
}

QString copyOptionLabel(const VLMS::Repositories::LoanCopyOption& copy) {
    const QString code = qs(copy.globalCopyId.empty() ? copy.localId : copy.globalCopyId);
    if (copy.authorName.empty()) {
        return QStringLiteral("%1 — %2").arg(code, qs(copy.bookTitle));
    }
    return QStringLiteral("%1 — %2 (%3)").arg(code, qs(copy.bookTitle), qs(copy.authorName));
}

}  // namespace

LoanCheckoutDialog::LoanCheckoutDialog(VLMS::Repositories::CirculationRepository& repository,
                                       const LoanScope& scope,
                                       QWidget* parent)
    : QDialog(parent),
      m_repository(repository),
      m_scope(scope) {
    buildUi();
    refreshMembers();
    refreshCopies();
    retranslateUi();
}

void LoanCheckoutDialog::buildUi() {
    resize(560, 420);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    VLMS::configureFormLayout(form);

    if (m_scope.memberId > 0) {
        // The member is already decided; the name is shown, not chosen.
        m_memberFixedLabel = new QLabel(m_scope.label, this);
        form->addRow(new QLabel(this), m_memberFixedLabel);
    } else {
        m_memberSearchEdit = new QLineEdit(this);
        connect(m_memberSearchEdit, &QLineEdit::textChanged, this, [this](const QString&) {
            refreshMembers();
        });
        form->addRow(new QLabel(this), m_memberSearchEdit);

        m_memberCombo = new QComboBox(this);
        form->addRow(new QLabel(this), m_memberCombo);
    }

    // Searching the whole library from a dialog titled after one book is how a
    // librarian lends the wrong one, so a fixed book takes the search row away.
    if (m_scope.bookId <= 0) {
        m_copySearchEdit = new QLineEdit(this);
        connect(m_copySearchEdit, &QLineEdit::textChanged, this, [this](const QString&) {
            refreshCopies();
        });
        form->addRow(new QLabel(this), m_copySearchEdit);
    }

    m_copyCombo = new QComboBox(this);
    form->addRow(new QLabel(this), m_copyCombo);

    m_borrowedDateEdit = new QDateEdit(this);
    m_borrowedDateEdit->setCalendarPopup(true);
    VLMS::setIsoDateFormat(m_borrowedDateEdit);
    m_borrowedDateEdit->setDate(qd(Clock::today()));
    // The UI half of finding 4. Without a ceiling the field keeps QDateEdit's
    // default of 9999-12-31 and the calendar popup hands over next month with
    // one click -- a checkout that has not happened yet. Core refuses it now;
    // this stops the librarian being offered it in the first place.
    m_borrowedDateEdit->setMaximumDate(qd(Clock::today()));
    form->addRow(new QLabel(this), m_borrowedDateEdit);

    m_dueDateEdit = new QDateEdit(this);
    m_dueDateEdit->setCalendarPopup(true);
    VLMS::setIsoDateFormat(m_dueDateEdit);
    m_dueDateEdit->setDate(qd(VLMS::Repositories::LoanPolicy::suggestedDueDate(Clock::today())));
    form->addRow(new QLabel(this), m_dueDateEdit);

    connect(m_borrowedDateEdit, &QDateEdit::dateChanged, this, [this](const QDate& date) {
        m_dueDateEdit->setDate(qd(VLMS::Repositories::LoanPolicy::suggestedDueDate(cd(date))));
    });

    m_notesEdit = new QPlainTextEdit(this);
    m_notesEdit->setMaximumHeight(90);
    form->addRow(new QLabel(this), m_notesEdit);

    layout->addLayout(form);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    VLMS::localizeButtonBox(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (m_scope.memberId <= 0 && m_memberCombo->currentData().toLongLong() <= 0) {
            VLMS::showWarning(
                this,
                T("loan.validation"),
                T("loan.memberRequired"));
            return;
        }
        if (m_copyCombo->currentData().toLongLong() <= 0) {
            VLMS::showWarning(
                this,
                T("loan.validation"),
                T("loan.copyRequired"));
            return;
        }
        if (m_dueDateEdit->date() < m_borrowedDateEdit->date()) {
            VLMS::showWarning(
                this,
                T("loan.validation"),
                T("loan.dueBeforeBorrow"));
            return;
        }
        accept();
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);
}

void LoanCheckoutDialog::retranslateUi() {
    setWindowTitle(m_scope.label.isEmpty()
                       ? T("loan.checkoutTitle")
                       : QStringLiteral("%1 — %2").arg(T("loan.checkoutTitle"), m_scope.label));

    VLMS::retranslateStandardButtons(findChild<QDialogButtonBox*>());

    QFormLayout* form = findChild<QFormLayout*>();
    if (form == nullptr) {
        return;
    }

    const auto setLabel = [&](int row, const QString& key) {
        if (QLayoutItem* item = form->itemAt(row, QFormLayout::LabelRole)) {
            if (auto* label = qobject_cast<QLabel*>(item->widget())) {
                label->setText(T(ss(key)));
            }
        }
    };

    // The scope decides which rows exist, so the indices are counted, not fixed.
    int row = 0;
    if (m_scope.memberId > 0) {
        setLabel(row++, QStringLiteral("loan.field.member"));
    } else {
        setLabel(row++, QStringLiteral("loan.field.memberSearch"));
        setLabel(row++, QStringLiteral("loan.field.member"));
    }
    if (m_scope.bookId <= 0) {
        setLabel(row++, QStringLiteral("loan.field.copySearch"));
    }
    setLabel(row++, QStringLiteral("loan.field.copy"));
    setLabel(row++, QStringLiteral("loan.field.borrowedAt"));
    setLabel(row++, QStringLiteral("loan.field.dueAt"));
    setLabel(row++, QStringLiteral("loan.field.notes"));

    if (m_memberSearchEdit != nullptr) {
        m_memberSearchEdit->setPlaceholderText(T("loan.memberSearchHint"));
    }
    if (m_copySearchEdit != nullptr) {
        m_copySearchEdit->setPlaceholderText(T("loan.copySearchHint"));
    }

    refreshMembers();
    refreshCopies();
}

void LoanCheckoutDialog::refreshMembers() {
    if (m_scope.memberId > 0) {
        return;  // no combo to fill; the name is a label
    }
    const qint64 currentId = m_memberCombo->currentData().toLongLong();
    m_memberCombo->clear();

    const auto membersResult = m_repository.listBorrowableMembers(ss(m_memberSearchEdit->text()));
    if (!membersResult) {
        VLMS::showRepoError(this, membersResult.error());
        return;
    }
    const auto& members = membersResult.value();
    if (members.empty()) {
        m_memberCombo->addItem(T("loan.noMembers"), 0);
        return;
    }

    for (const VLMS::Repositories::LoanMemberOption& member : members) {
        m_memberCombo->addItem(memberOptionLabel(member), QVariant::fromValue(member.id));
    }

    const int index = m_memberCombo->findData(currentId);
    if (index >= 0) {
        m_memberCombo->setCurrentIndex(index);
    }
}

void LoanCheckoutDialog::refreshCopies() {
    const qint64 currentId = m_copyCombo->currentData().toLongLong();
    m_copyCombo->clear();

    const std::string search =
        m_copySearchEdit == nullptr ? std::string() : ss(m_copySearchEdit->text());
    const auto copiesResult = m_repository.listAvailableCopies(search, m_scope.bookId);
    if (!copiesResult) {
        VLMS::showRepoError(this, copiesResult.error());
        return;
    }
    const auto& copies = copiesResult.value();
    if (copies.empty()) {
        // Every copy is out. The dialog still opens and says so — the history
        // behind it names who holds each one.
        m_copyCombo->addItem(T("loan.noCopies"), 0);
        if (m_buttons != nullptr) {
            m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        }
        return;
    }
    if (m_buttons != nullptr) {
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    }

    for (const VLMS::Repositories::LoanCopyOption& copy : copies) {
        m_copyCombo->addItem(copyOptionLabel(copy), QVariant::fromValue(copy.id));
    }

    const int index = m_copyCombo->findData(currentId);
    if (index >= 0) {
        m_copyCombo->setCurrentIndex(index);
    }
}

VLMS::Repositories::LoanInput LoanCheckoutDialog::loanInput() const {
    VLMS::Repositories::LoanInput input;
    input.memberId = m_scope.memberId > 0 ? m_scope.memberId
                                          : m_memberCombo->currentData().toLongLong();
    input.bookCopyId = m_copyCombo->currentData().toLongLong();
    input.borrowedAt = ss(m_borrowedDateEdit->date().toString(Qt::ISODate));
    input.dueAt = ss(m_dueDateEdit->date().toString(Qt::ISODate));
    input.notes = ss(m_notesEdit->toPlainText());
    return input;
}

bool LoanCheckoutDialog::hasLendableCopy() const {
    return m_copyCombo != nullptr && m_copyCombo->currentData().toLongLong() > 0;
}
