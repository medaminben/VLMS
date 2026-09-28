#include "ui/circulation/LoanExtendDialog.h"

#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Clock.h>
#include <VLMS/Repositories/LoanPolicy.h>
#include <VLMS/Core/Strings.h>
#include "ui/UiHelpers.h"
#include "QtBridge.h"

#include <QDate>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

using VLMS::T;
using VLMS::cd;
using VLMS::qd;
using VLMS::qs;
using VLMS::qsl;
using VLMS::ss;
using VLMS::svl;

namespace {

}  // namespace

LoanExtendDialog::LoanExtendDialog(const VLMS::Repositories::LoanRecord& loan, QWidget* parent)
    : QDialog(parent) {
    buildUi(loan);
    retranslateUi(loan);
}

void LoanExtendDialog::buildUi(const VLMS::Repositories::LoanRecord& loan) {
    resize(480, 240);

    auto* layout = new QVBoxLayout(this);

    auto* summary = new QLabel(this);
    summary->setObjectName(QStringLiteral("loanExtendSummary"));
    summary->setWordWrap(true);
    layout->addWidget(summary);

    auto* form = new QFormLayout();
    VLMS::configureFormLayout(form);

    m_currentDueLabel = new QLabel(this);
    form->addRow(new QLabel(this), m_currentDueLabel);

    m_dueDateEdit = new QDateEdit(this);
    m_dueDateEdit->setCalendarPopup(true);
    VLMS::setIsoDateFormat(m_dueDateEdit);

    const QDate currentDue = QDate::fromString(qs(loan.dueAt), Qt::ISODate);
    const auto today = VLMS::Core::Clock::today();
    m_dueDateEdit->setMinimumDate(qd(VLMS::Repositories::LoanPolicy::minimumExtensionDate(cd(currentDue), today)));
    m_dueDateEdit->setDate(qd(VLMS::Repositories::LoanPolicy::suggestedExtensionDate(cd(currentDue), today)));
    form->addRow(new QLabel(this), m_dueDateEdit);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    VLMS::localizeButtonBox(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, loan]() {
        const QDate currentDueDate = QDate::fromString(qs(loan.dueAt), Qt::ISODate);
        if (currentDueDate.isValid() && m_dueDateEdit->date() <= currentDueDate) {
            VLMS::showWarning(
                this,
                T("loan.validation"),
                T("loan.extendInvalid"));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void LoanExtendDialog::retranslateUi(const VLMS::Repositories::LoanRecord& loan) {
    setWindowTitle(T("loan.extendTitle"));

    VLMS::retranslateStandardButtons(findChild<QDialogButtonBox*>());

    if (auto* summary = findChild<QLabel*>(QStringLiteral("loanExtendSummary"))) {
        QString text = T("loan.returnSummary");
        text.replace(QStringLiteral("{member}"), qs(loan.memberName));
        text.replace(QStringLiteral("{number}"), qs(loan.membershipNumber));
        text.replace(QStringLiteral("{title}"), qs(loan.bookTitle));
        text.replace(QStringLiteral("{copy}"), qs(loan.copyCode));
        summary->setText(text);
    }

    m_currentDueLabel->setText(qs(loan.dueAt));

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

    setLabel(0, QStringLiteral("loan.field.currentDueAt"));
    setLabel(1, QStringLiteral("loan.field.newDueAt"));
}

QString LoanExtendDialog::dueAt() const {
    return m_dueDateEdit->date().toString(Qt::ISODate);
}
