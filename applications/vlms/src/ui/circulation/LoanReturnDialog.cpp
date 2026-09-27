#include "ui/circulation/LoanReturnDialog.h"

#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Strings.h>
#include "ui/UiHelpers.h"
#include "QtBridge.h"

#include <QDate>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
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

}  // namespace

LoanReturnDialog::LoanReturnDialog(const LoanRecord& loan, QWidget* parent)
    : QDialog(parent) {
    buildUi(loan);
    retranslateUi(loan);
}

void LoanReturnDialog::buildUi(const LoanRecord& loan) {
    resize(480, 280);

    auto* layout = new QVBoxLayout(this);

    auto* summary = new QLabel(this);
    summary->setObjectName(QStringLiteral("loanReturnSummary"));
    summary->setWordWrap(true);
    layout->addWidget(summary);

    auto* form = new QFormLayout();
    VLMS::configureFormLayout(form);

    m_returnedDateEdit = new QDateEdit(this);
    m_returnedDateEdit->setCalendarPopup(true);
    VLMS::setIsoDateFormat(m_returnedDateEdit);
    m_returnedDateEdit->setDate(qd(Clock::today()));
    m_returnedDateEdit->setMaximumDate(qd(Clock::today()));
    const QDate borrowed = QDate::fromString(qs(loan.borrowedAt), Qt::ISODate);
    if (borrowed.isValid()) {
        m_returnedDateEdit->setMinimumDate(borrowed);
    }
    form->addRow(new QLabel(this), m_returnedDateEdit);

    m_notesEdit = new QPlainTextEdit(this);
    m_notesEdit->setMaximumHeight(100);
    m_notesEdit->setPlainText(qs(loan.notes));
    form->addRow(new QLabel(this), m_notesEdit);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    VLMS::localizeButtonBox(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (m_returnedDateEdit->date() > qd(Clock::today())) {
            VLMS::showWarning(
                this,
                T("loan.validation"),
                T("loan.returnAfterToday"));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void LoanReturnDialog::retranslateUi(const LoanRecord& loan) {
    setWindowTitle(T("loan.returnTitle"));

    VLMS::retranslateStandardButtons(findChild<QDialogButtonBox*>());

    if (auto* summary = findChild<QLabel*>(QStringLiteral("loanReturnSummary"))) {
        QString text = T("loan.returnSummary");
        text.replace(QStringLiteral("{member}"), qs(loan.memberName));
        text.replace(QStringLiteral("{number}"), qs(loan.membershipNumber));
        text.replace(QStringLiteral("{title}"), qs(loan.bookTitle));
        text.replace(QStringLiteral("{copy}"), qs(loan.copyCode));
        summary->setText(text);
    }

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

    setLabel(0, QStringLiteral("loan.field.returnedAt"));
    setLabel(1, QStringLiteral("loan.field.notes"));
}

QString LoanReturnDialog::returnedAt() const {
    return m_returnedDateEdit->date().toString(Qt::ISODate);
}

QString LoanReturnDialog::notes() const {
    return m_notesEdit->toPlainText();
}
